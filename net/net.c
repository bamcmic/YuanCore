/* net/net.c - 以太网 / ARP / IPv4 / UDP 最小协议栈
 *
 * 出站:eth(arp|ip) -> ARP 请求、net_ip_send(通用 IP)、udp_send(直组包)
 * 入站:net_poll() 逐帧分发 -> ARP(reply/request 都学习) /
 *       ICMP(icmp_handle) / TCP(预留,下一批接 tcp_handle) / UDP 回调
 * IP 头校验和发送必算(网关会丢错的),接收侧也验证;UDP 校验和置 0
 * (IPv4 允许"不校验"),收包方向不查 —— 维持最小正确实现。
 *
 * 包缓冲:静态单包缓冲(发/收各一)。shell 命令是同步阻塞调用,
 * 不存在并发,够用且零分配。
 */
#include "net.h"
#include "netcfg.h"
#include "pci.h"
#include "e1000.h"
#include "icmp.h"
#include "tcp.h"
#include "../mode/timer.h"

/* 前向声明(本文件内部收包入口,arp_resolve 轮询时会用) */
int net_poll_frame(void);

/* ---- 以太网 ---- */
#define ETH_ALEN        6u
#define ETHERTYPE_ARP   0x0806u
#define ETHERTYPE_IPV4  0x0800u

struct eth_hdr {
    unsigned char dst[ETH_ALEN];
    unsigned char src[ETH_ALEN];
    unsigned short ethertype;
} __attribute__((packed));

/* ---- ARP ---- */
#define ARP_HW_ETHER  1u
#define ARP_OP_REQUEST  1u
#define ARP_OP_REPLY    2u

struct arp_hdr {
    unsigned short hw_type;
    unsigned short proto;
    unsigned char hw_len;
    unsigned char proto_len;
    unsigned short op;
    unsigned char sha[ETH_ALEN];
    unsigned int  spa;
    unsigned char tha[ETH_ALEN];
    unsigned int  tpa;
} __attribute__((packed));

/* ---- IPv4 ---- */
struct ip_hdr {
    unsigned char  ver_ihl;      /* 高 4 位版本,低 4 位 IHL(按 4 字节计) */
    unsigned char  tos;
    unsigned short total_len;
    unsigned short id;
    unsigned short flags_frag;
    unsigned char  ttl;
    unsigned char  proto;
    unsigned short checksum;
    unsigned int   src;
    unsigned int   dst;
} __attribute__((packed));

#define IP_DEFAULT_TTL 64u

/* ---- UDP ---- */
struct udp_hdr {
    unsigned short src_port;
    unsigned short dst_port;
    unsigned short length;       /* 含头部 */
    unsigned short checksum;
} __attribute__((packed));

/* ---- 状态 ---- */
static unsigned char host_mac[ETH_ALEN];   /* 网卡自身 MAC */
static int           net_up;               /* net_init 成功标志 */
static unsigned int  net_bar0;             /* PCI BAR0,netinfo 展示用 */
static unsigned char net_pci_irq;          /* PCI 中断线,展示用 */

/* ---- ARP 表(8 条 + 老化) ---- */
#define ARP_TABLE_MAX  8
#define ARP_TTL_TICKS  (300u * 100u)       /* 5 分钟 @ 100Hz */

static struct {
    unsigned int  ip;                    /* 主机序 */
    unsigned char mac[ETH_ALEN];
    unsigned int  stamp;                 /* 学习时刻 tick */
    int           valid;
} arp_tab[ARP_TABLE_MAX];

/* UDP 端口回调表(同端口重复注册 = 覆盖,防表被长跑应用撑爆) */
#define UDP_LISTEN_MAX 4
static struct {
    unsigned short  port;
    udp_handler_fn  fn;
} udp_listeners[UDP_LISTEN_MAX];
static int udp_listener_count;

/* 收发工作缓冲(单包) */
static unsigned char send_buf[NET_RXBUF_SZ];
static unsigned char recv_buf[NET_RXBUF_SZ];

static const unsigned char eth_bcast[ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

void net_get_mac(unsigned char mac[6])
{
    int i;

    for (i = 0; i < 6; i++)
        mac[i] = host_mac[i];
}

int net_ready(void)
{
    return net_up;
}

void udp_listen(unsigned short port, udp_handler_fn fn)
{
    int i;

    if (fn == 0)
        return;
    /* 同端口覆盖,不占新槽(ntp/dns 重复初始化的安全垫) */
    for (i = 0; i < udp_listener_count; i++) {
        if (udp_listeners[i].port == port) {
            udp_listeners[i].fn = fn;
            return;
        }
    }
    if (udp_listener_count >= UDP_LISTEN_MAX)
        return;
    udp_listeners[udp_listener_count].port = port;
    udp_listeners[udp_listener_count].fn   = fn;
    udp_listener_count++;
}

/* ---- 以太网发送 ---- */

static int eth_send(const unsigned char dst[ETH_ALEN],
                    unsigned short ethertype,
                    const unsigned char *payload, unsigned int len)
{
    struct eth_hdr *eth = (struct eth_hdr *)send_buf;

    if (len + sizeof(struct eth_hdr) > sizeof(send_buf))
        return -1;
    net_memcpy(eth->dst, dst, ETH_ALEN);
    net_memcpy(eth->src, host_mac, ETH_ALEN);
    eth->ethertype = htons16(ethertype);
    net_memcpy(send_buf + sizeof(struct eth_hdr), payload, len);

    return e1000_send(send_buf, len + sizeof(struct eth_hdr));
}

/* ---- ARP ---- */

static void arp_learn(unsigned int ip, const unsigned char mac[ETH_ALEN])
{
    int i, victim = 0;
    unsigned int oldest;

    /* 已有该 ip -> 原地刷新 */
    for (i = 0; i < ARP_TABLE_MAX; i++) {
        if (arp_tab[i].valid && arp_tab[i].ip == ip) {
            net_memcpy(arp_tab[i].mac, mac, ETH_ALEN);
            arp_tab[i].stamp = timer_ticks();
            return;
        }
    }
    /* 空槽优先;否则挤掉最老 */
    oldest = 0xFFFFFFFFu;
    for (i = 0; i < ARP_TABLE_MAX; i++) {
        if (!arp_tab[i].valid) {
            victim = i;
            break;
        }
        if (arp_tab[i].stamp < oldest) {
            oldest = arp_tab[i].stamp;
            victim = i;
        }
    }
    arp_tab[victim].ip    = ip;
    net_memcpy(arp_tab[victim].mac, mac, ETH_ALEN);
    arp_tab[victim].stamp = timer_ticks();
    arp_tab[victim].valid = 1;
}

static int arp_lookup(unsigned int ip, unsigned char out_mac[ETH_ALEN])
{
    int i;

    for (i = 0; i < ARP_TABLE_MAX; i++) {
        if (arp_tab[i].valid && arp_tab[i].ip == ip) {
            if (timer_ticks() - arp_tab[i].stamp >= ARP_TTL_TICKS) {
                arp_tab[i].valid = 0;      /* 过期 */
                return 0;
            }
            net_memcpy(out_mac, arp_tab[i].mac, ETH_ALEN);
            return 1;
        }
    }
    return 0;
}

int arp_resolve(unsigned int ip, unsigned char out_mac[6])
{
    struct arp_hdr *arp = (struct arp_hdr *)(send_buf
                          + sizeof(struct eth_hdr));
    unsigned int start;
    unsigned int tries;

    if (!net_up)
        return -1;
    if (arp_lookup(ip, out_mac))
        return 0;

    /* 广播 ARP request:谁有 ip,告诉 host_mac */
    net_memset(arp, 0, sizeof(struct arp_hdr));
    arp->hw_type   = htons16(ARP_HW_ETHER);
    arp->proto     = htons16(ETHERTYPE_IPV4);
    arp->hw_len    = ETH_ALEN;
    arp->proto_len = 4;
    arp->op        = htons16(ARP_OP_REQUEST);
    net_memcpy(arp->sha, host_mac, ETH_ALEN);
    arp->spa = htonl32(netcfg_ip());
    net_memset(arp->tha, 0, ETH_ALEN);
    arp->tpa = htonl32(ip);

    if (eth_send(eth_bcast, ETHERTYPE_ARP, send_buf + sizeof(struct eth_hdr),
                 sizeof(struct arp_hdr)) != 0)
        return -1;

    /* 轮询等 reply(2 秒;timer 100Hz,200 tick) */
    start = timer_ticks();
    for (tries = 0; tries < 200000; tries++) {
        if (net_poll_frame() > 0 && arp_lookup(ip, out_mac))
            return 0;
        if (timer_ticks() - start >= 200)   /* 200 ticks = 2s */
            break;
    }
    return -1;
}

/* ---- 路由决策:目标 IP -> 下一跳以太网地址 ---- */

static int ip_is_broadcast(unsigned int ip)
{
    unsigned int mask = netcfg_netmask();

    return ip == 0xFFFFFFFFu || (ip & ~mask) == ~mask;
}

static int route_peer_mac(unsigned int dst_ip, unsigned char mac[ETH_ALEN])
{
    unsigned int mask = netcfg_netmask();

    /* 同网段直连;否则(含广播应答目标)走网关 */
    if ((dst_ip & mask) == (netcfg_ip() & mask))
        return arp_resolve(dst_ip, mac);
    return arp_resolve(netcfg_gateway(), mac);
}

/* 对外导出:上层(ntp 预检等)需要与实际发包路径一致的 ARP 预热 */
int net_route_peer_mac(unsigned int dst_ip, unsigned char mac[6])
{
    return route_peer_mac(dst_ip, mac);
}

/* ---- IPv4 校验和(RFC1071,ICMP/TCP 也要用) ---- */

unsigned short net_checksum(const unsigned char *data, unsigned int len)
{
    unsigned int sum = 0;

    while (len > 1) {
        sum += (unsigned int)((unsigned short)data[0] << 8
                            | data[1]);   /* 大端求和 */
        data += 2;
        len  -= 2;
    }
    if (len == 1)
        sum += (unsigned int)((unsigned short)data[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (unsigned short)(~sum & 0xFFFFu);
}

/* ---- 通用 IP 出站(icmp/tcp 用;udp_send 保留直组包路径) ---- */

int net_ip_send(unsigned char proto, unsigned int dst_ip,
                const unsigned char *packet, unsigned int len)
{
    struct ip_hdr *ip;
    unsigned int   ip_total;
    unsigned char  peer_mac[ETH_ALEN];
    const unsigned char *eth_dst;

    if (!net_up)
        return -1;
    if (len + sizeof(struct ip_hdr) > sizeof(send_buf)
                                      - sizeof(struct eth_hdr))
        return -1;

    if (ip_is_broadcast(dst_ip))
        eth_dst = eth_bcast;
    else {
        if (route_peer_mac(dst_ip, peer_mac) != 0)
            return -1;
        eth_dst = peer_mac;
    }

    ip_total = len + sizeof(struct ip_hdr);
    net_memcpy(send_buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr),
               packet, len);

    ip = (struct ip_hdr *)(send_buf + sizeof(struct eth_hdr));
    ip->ver_ihl     = 0x45;               /* IPv4 + 20 字节头 */
    ip->tos         = 0;
    ip->total_len   = htons16((unsigned short)ip_total);
    ip->id          = 0;
    ip->flags_frag  = 0;                  /* 不分片 */
    ip->ttl         = IP_DEFAULT_TTL;
    ip->proto       = proto;
    ip->checksum    = 0;
    ip->src         = htonl32(netcfg_ip());
    ip->dst         = htonl32(dst_ip);
    ip->checksum    = htons16(net_checksum((unsigned char *)ip,
                                   sizeof(struct ip_hdr)));

    return eth_send(eth_dst, ETHERTYPE_IPV4,
                    send_buf + sizeof(struct eth_hdr), ip_total);
}

/* ---- UDP 出站 ---- */

int udp_send(unsigned int dst_ip, unsigned short dst_port,
             unsigned short src_port,
             const unsigned char *payload, unsigned int len)
{
    unsigned char peer_mac[ETH_ALEN];
    const unsigned char *eth_dst;
    struct ip_hdr  *ip;
    struct udp_hdr *udp;
    unsigned int    udp_len, ip_total;
    unsigned int    payload_off;

    if (!net_up)
        return -1;
    if (len + sizeof(struct udp_hdr) + sizeof(struct ip_hdr)
        + sizeof(struct eth_hdr) > sizeof(send_buf))
        return -1;

    if (ip_is_broadcast(dst_ip))
        eth_dst = eth_bcast;
    else {
        if (route_peer_mac(dst_ip, peer_mac) != 0)
            return -1;
        eth_dst = peer_mac;
    }

    payload_off = sizeof(struct eth_hdr) + sizeof(struct ip_hdr)
                + sizeof(struct udp_hdr);
    udp_len  = len + sizeof(struct udp_hdr);
    ip_total = udp_len + sizeof(struct ip_hdr);

    net_memcpy(send_buf + payload_off, payload, len);

    udp = (struct udp_hdr *)(send_buf + sizeof(struct eth_hdr)
                           + sizeof(struct ip_hdr));
    udp->src_port = htons16(src_port);
    udp->dst_port = htons16(dst_port);
    udp->length   = htons16((unsigned short)udp_len);
    udp->checksum = 0;                    /* IPv4 允许 0 = 不校验 */

    ip = (struct ip_hdr *)(send_buf + sizeof(struct eth_hdr));
    ip->ver_ihl     = 0x45;               /* IPv4 + 20 字节头 */
    ip->tos         = 0;
    ip->total_len   = htons16((unsigned short)ip_total);
    ip->id          = 0;
    ip->flags_frag  = 0;                  /* 不分片 */
    ip->ttl         = IP_DEFAULT_TTL;
    ip->proto       = IP_PROTO_UDP;
    ip->checksum    = 0;
    ip->src         = htonl32(netcfg_ip());
    ip->dst         = htonl32(dst_ip);
    ip->checksum    = htons16(net_checksum((unsigned char *)ip,
                                   sizeof(struct ip_hdr)));

    return eth_send(eth_dst, ETHERTYPE_IPV4,
                    send_buf + sizeof(struct eth_hdr), ip_total);
}

/* ---- 接收分发 ---- */

/* 发 ARP reply:本机 ip/mac 回答给 asker(单播) */
static void arp_send_reply(unsigned int asker_ip,
                           const unsigned char asker_mac[ETH_ALEN])
{
    struct arp_hdr *arp = (struct arp_hdr *)(send_buf
                          + sizeof(struct eth_hdr));

    net_memset(arp, 0, sizeof(struct arp_hdr));
    arp->hw_type   = htons16(ARP_HW_ETHER);
    arp->proto     = htons16(ETHERTYPE_IPV4);
    arp->hw_len    = ETH_ALEN;
    arp->proto_len = 4;
    arp->op        = htons16(ARP_OP_REPLY);
    net_memcpy(arp->sha, host_mac, ETH_ALEN);
    arp->spa = htonl32(netcfg_ip());
    net_memcpy(arp->tha, asker_mac, ETH_ALEN);
    arp->tpa = htonl32(asker_ip);

    eth_send(asker_mac, ETHERTYPE_ARP,
             send_buf + sizeof(struct eth_hdr), sizeof(struct arp_hdr));
}

static void handle_arp(const struct arp_hdr *arp)
{
    unsigned int spa = ntohl32(arp->spa);
    unsigned int tpa = ntohl32(arp->tpa);

    if (ntohs16(arp->op) == ARP_OP_REPLY && tpa == netcfg_ip()) {
        arp_learn(spa, arp->sha);          /* reply:目标端映射 */
        return;
    }
    if (ntohs16(arp->op) == ARP_OP_REQUEST) {
        /* 请求者也顺带学习(免费缓存) */
        if (spa != 0)
            arp_learn(spa, arp->sha);
        /* 回答针对本机 IP 的请求 —— 标准 ARP 行为。hostfwd 反向
         * 解析(guest 从未主动发包时)完全依赖它。 */
        if (tpa == netcfg_ip() && spa != 0)
            arp_send_reply(spa, arp->sha);
    }
}

static void handle_udp(const struct ip_hdr *ip, unsigned int ihl_bytes,
                       unsigned int frame_left)
{
    struct udp_hdr *udp;
    unsigned int    udp_len;
    int i;

    if (frame_left < sizeof(struct udp_hdr))
        return;

    udp = (struct udp_hdr *)((unsigned char *)ip + ihl_bytes);
    udp_len = ntohs16(udp->length);
    if (udp_len < sizeof(struct udp_hdr))
        return;
    if (udp_len > frame_left)
        return;

    for (i = 0; i < udp_listener_count; i++) {
        if (udp_listeners[i].port == ntohs16(udp->dst_port)) {
            udp_listeners[i].fn((const unsigned char *)udp
                                  + sizeof(struct udp_hdr),
                                udp_len - sizeof(struct udp_hdr),
                                ntohl32(ip->src),
                                ntohs16(udp->src_port));
            return;
        }
    }
}

/* 处理一帧。返回 1=收到了一帧,0=无帧 */
int net_poll_frame(void)
{
    unsigned int len;
    struct eth_hdr *eth;
    unsigned int ethertype;
    unsigned int frame_left;

    if (!net_up)
        return 0;

    len = e1000_recv(recv_buf, sizeof(recv_buf));
    if (len < sizeof(struct eth_hdr))
        return 0;

    eth = (struct eth_hdr *)recv_buf;
    ethertype = ntohs16(eth->ethertype);
    frame_left = len - sizeof(struct eth_hdr);



    if (ethertype == ETHERTYPE_ARP) {
        if (frame_left >= sizeof(struct arp_hdr))
            handle_arp((const struct arp_hdr *)(recv_buf
                         + sizeof(struct eth_hdr)));
        return 1;
    }
    if (ethertype == ETHERTYPE_IPV4) {
        struct ip_hdr *ip = (struct ip_hdr *)(recv_buf
                              + sizeof(struct eth_hdr));
        unsigned int ihl;

        if (frame_left < sizeof(struct ip_hdr))
            return 1;
        if ((ip->ver_ihl >> 4) != 4)
            return 1;

        /* IP 头校验和:错了直接丢(对端/线路损坏) */
        if (net_checksum((unsigned char *)ip,
                         (unsigned int)(ip->ver_ihl & 0x0Fu) * 4u) != 0)
            return 1;

        if (ntohl32(ip->dst) != netcfg_ip()
            && ntohl32(ip->dst) != 0xFFFFFFFFu)
            return 1;
        ihl = (unsigned int)(ip->ver_ihl & 0x0Fu) * 4u;
        if (ihl < sizeof(struct ip_hdr) || ihl > frame_left)
            return 1;
        /* 以太最小帧的 padding 不属于 IP 载荷:按 IP 总长截断,
         * 否则 TCP/UDP 的 dlen 会混入垃圾字节(SYN 被丢/确认号漂移) */
        {
            unsigned int ip_total = ntohs16(ip->total_len);

            if (ip_total < ihl || ip_total > frame_left)
                return 1;
            frame_left = ip_total;
        }

        if (ip->proto == IP_PROTO_UDP)
            handle_udp(ip, ihl, frame_left - ihl);
        else if (ip->proto == IP_PROTO_ICMP)
            icmp_handle((const unsigned char *)ip + ihl,
                        frame_left - ihl, ntohl32(ip->src));
        else if (ip->proto == IP_PROTO_TCP)
            tcp_handle((const unsigned char *)ip + ihl,
                       frame_left - ihl, ntohl32(ip->src));
        return 1;
    }
    return 1;                             /* 其他以太类型:吞掉即可 */
}

/* ---- 初始化 ---- */

int net_init(void)
{
    unsigned int bar0;
    unsigned char irq;

    net_up = 0;
    {
        int i;

        for (i = 0; i < ARP_TABLE_MAX; i++)
            arp_tab[i].valid = 0;
    }
    udp_listener_count = 0;
    netcfg_defaults();
    netcfg_load();                        /* /net.cfg 覆盖默认值(fs 已就绪) */

    if (pci_find_device(PCI_VENDOR_INTEL, PCI_DEVICE_E1000,
                        &bar0, &irq) != 0)
        return -1;

    if (e1000_init(bar0) != 0)
        return -2;

    /* Bus Master 使能必须在 e1000 软复位之后:QEMU 对 CTRL.RST
     * 连带 pci_device_reset,会把 command 寄存器打回默认。
     * 没有它 QEMU 8.2.x 的 e1000 不发起 DMA,收发全瘫。 */
    pci_enable_master();

    net_bar0 = bar0;
    net_pci_irq = irq;
    e1000_get_mac(host_mac);
    e1000_link_up();                      /* 结果只影响可用性,不阻塞成功 */

    net_up = 1;
    return 0;
}

/* PCI 找到的 BAR0/IRQ 供 netinfo 命令展示 */
unsigned int net_pci_bar0(void)
{
    return net_bar0;
}

unsigned char net_pci_irq_line(void)
{
    return net_pci_irq;
}
