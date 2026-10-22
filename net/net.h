/* net/net.h - YuanCore 网络子系统公共头
 *
 * 分层:
 *   net/pci.c    PCI 总线枚举(找网卡,读 BAR/IRQ)
 *   net/e1000.c  Intel 82540EM 网卡驱动(QEMU 默认虚拟卡),轮询收发
 *   net/netcfg.c 配置状态(IP/掩码/网关/DNS/NTP)+ /net.cfg 持久化
 *   net/net.c    以太网帧 / ARP(8 条表) / IPv4 / UDP 最小协议栈
 *   net/icmp.c   ICMP echo(ping)
 *   net/dns.c    DNS A 记录解析(net_resolve 统一入口)
 *   net/ntp.c    NTP 客户端(UDP:123 -> 写 RTC)
 *
 * 典型路径:
 *   ping 10.0.2.2   -> net_resolve -> icmp_ping -> arp_resolve -> ip_send
 *   ntpsync ntp.aliyun.com -> net_resolve -> ntp_sync -> udp 收发 -> rtc_write
 *
 * 设计取舍(稳定优先):
 *   - 收包走轮询,不用网卡中断(绕开 PCI IRQ 路由问题;中断化单列任务)
 *   - IP/网关/DNS/NTP 出厂默认为 QEMU user network 标准值,
 *     可经 `net set` 运行时修改并持久化到 /net.cfg
 *   - IP 报文出站统一走 net_ip_send(icmp/tcp 共用);UDP 保留直组包路径
 *   - 全程 32 位运算,不碰 64 位除法
 *   - TX/RX 描述符环与缓冲全部静态分配:恒等映射下物理地址=虚拟地址
 *     且 < 4GB,天然满足 e1000 的 32 位 DMA 约束
 */
#ifndef NET_NET_H
#define NET_NET_H

#include "../mode/io.h"

/* ---- 网络字节序 <-> 主机序(x86 小端) ---- */
static inline unsigned int ntohl32(unsigned int v)
{
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8)
         | ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}
static inline unsigned int htonl32(unsigned int v) { return ntohl32(v); }
static inline unsigned short ntohs16(unsigned short v)
{
    return (unsigned short)(((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu));
}
static inline unsigned short htons16(unsigned short v) { return ntohs16(v); }

static inline void net_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n--)
        *d++ = *s++;
}

static inline void net_memset(void *dst, unsigned char c, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n--)
        *d++ = c;
}

/* ---- 协议常量 ---- */
#define NET_MTU       1500u
#define NET_RXBUF_SZ  2048u         /* 对齐 e1000 RCTL.BSIZE=2048 */

#define IP_PROTO_ICMP   1u
#define IP_PROTO_TCP    6u
#define IP_PROTO_UDP    17u

#define NTP_PORT      123u

/* NTP 纪元(1900) 到 Unix 纪元(1970) 的秒差 */
#define NTP_EPOCH_DELTA 2208988800u

/* 中国标准时间 UTC+8:写入 CMOS 的墙钟按北京时间解释 */
#define TZ_OFFSET_SEC (8u * 3600u)

/* ---- 接口 ---- */

/* 初始化:PCI 找卡 -> e1000 复位/配环 -> 配置加载 -> 协议栈就绪。
 * 返回 0 成功;失败返回负值(无卡/复位失败),系统其余部分照常启动。 */
int net_init(void);

/* 是否就绪(找到网卡且初始化成功) */
int net_ready(void);

/* 本机 MAC(6 字节) */
void net_get_mac(unsigned char mac[6]);

/* RFC1071 16 位反码和(ICMP/TCP 校验和要用) */
unsigned short net_checksum(const unsigned char *data, unsigned int len);

/* 通用 IPv4 出站:packet = IP payload(ICMP/TCP 报文整体)。
 * 内部按目标 IP 路由(同网段直连 / 跨网段走网关 / 广播直发),
 * 封 IP 头(算校验和)后交以太网层。返回 0/负错误 */
int net_ip_send(unsigned char proto, unsigned int dst_ip,
                const unsigned char *packet, unsigned int len);

/* 阻塞式 UDP 收发:
 *   udp_send(dst_ip, dst_port, sport, payload, len)  返回 0/负错误
 *   net_poll()  处理一个到达的包(分发到 ARP/UDP 回调)
 * UDP 回调注册:UDP 端口 -> 处理函数(重复注册同端口 = 覆盖,不占新槽) */
typedef void (*udp_handler_fn)(const unsigned char *payload,
                               unsigned int len,
                               unsigned int src_ip,
                               unsigned short src_port);
void udp_listen(unsigned short port, udp_handler_fn fn);

int udp_send(unsigned int dst_ip, unsigned short dst_port,
             unsigned short src_port,
             const unsigned char *payload, unsigned int len);

/* ARP:解析 ip 的 MAC 到 out(6 字节)。会发请求并轮询等待,超时返回 -1 */
int arp_resolve(unsigned int ip, unsigned char out_mac[6]);

/* 路由决策:同网段 -> 解析目标本身;跨网段 -> 解析网关。
 * 上层在正式发包前做 ARP 预热/明确报错时使用 */
int net_route_peer_mac(unsigned int dst_ip, unsigned char out_mac[6]);

/* 处理一帧(轮询入口)。返回 1=收到一帧,0=无帧 */
int net_poll_frame(void);

/* netinfo 展示用 */
unsigned int net_pci_bar0(void);
unsigned char net_pci_irq_line(void);

/* NTP:阻塞同步到指定服务器。成功返回 0 并把 UTC+8 写入 RTC;
 * out_unix 秒、out_hms[3] 返回北京时间时分秒(供显示),可为 0 */
int ntp_sync(unsigned int server_ip, unsigned int *out_unix, int out_hms[3]);

#endif /* NET_NET_H */
