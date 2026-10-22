/* net/tcp.h - 单连接阻塞式 TCP(单 TCB)
 *
 * 定位:YuanAI YCP1 的传输底座。同一时刻只维护一条连接,
 * 全部状态在静态 TCB 里,零动态分配。
 *
 * 覆盖路径:
 *   主动: CLOSED -> SYN_SENT -> ESTABLISHED -> FIN_WAIT -> CLOSED
 *   被动: CLOSED -> LISTEN -> SYN_RCVD -> ESTABLISHED -> CLOSE_WAIT
 *         -> LAST_ACK -> CLOSED
 *   任一态收 RST: 复位为 CLOSED,阻塞等待者以错误码返回。
 *
 * 简化取舍(稳定优先):
 *   - 单段在途:send 每次只发一段(<=536B),等 ACK 前进再发下一段,
 *     阻塞语义天然匹配;重传即重发在途段,最多 3 次
 *   - 立即 ACK,不做延迟确认/窗口通告(固定大窗口,对端单段流量下够用)
 *   - 不做 MSS 选项协商:发段固定 <=536B(TCP 无选项时的互操作默认)
 *   - 32 位序号回绕用有符号差比较;全程无 64 位除法
 *   - 阻塞等待 = 轮询 net_poll_frame + hlt,不依赖网卡中断
 */
#ifndef NET_TCP_H
#define NET_TCP_H

#include "net.h"

/* TCB 状态(tcp_state 返回值) */
#define TCPST_CLOSED      0u
#define TCPST_LISTEN      1u
#define TCPST_SYN_SENT    2u
#define TCPST_SYN_RCVD    3u
#define TCPST_ESTABLISHED 4u
#define TCPST_FIN_WAIT1   5u
#define TCPST_FIN_WAIT2   6u
#define TCPST_CLOSE_WAIT  7u
#define TCPST_LAST_ACK    8u
#define TCPST_CLOSING     9u
#define TCPST_TIME_WAIT  10u

/* TCP 头部标志位 */
#define TCPF_FIN 0x01u
#define TCPF_SYN 0x02u
#define TCPF_RST 0x04u
#define TCPF_PSH 0x08u
#define TCPF_ACK 0x10u

/* 超时/重传(单位:tick,100Hz) */
#define TCP_RTO_TICKS     100u   /* 1s 重传间隔 */
#define TCP_MAX_RETRIES   3u
#define TCP_CONNECT_TICKS 1000u  /* 连接总超时 10s */
#define TCP_CLOSE_TICKS   500u   /* 关闭握手总超时 5s */

/* 接收环形缓冲 */
#define TCP_RXBUF 16384u

/* 单段上限:无 MSS 协商时的 TCP 互操作默认 */
#define TCP_MSS 536u

/* ---- API(全部阻塞语义,返回 0 成功 / 负错误) ---- */

int  tcp_init(void);

/* 主动连接。成功后进入 ESTABLISHED */
int  tcp_connect(unsigned int ip, unsigned short port);

/* 被动监听并等待第一条连接(内含 accept)。成功后 ESTABLISHED */
int  tcp_listen(unsigned short port);

/* 发送全部数据。阻塞直到全部被 ACK */
int  tcp_send(const unsigned char *data, unsigned int len);

/* 接收。返回读到字节数;0=对端已关闭(EOF);负=错误 */
int  tcp_recv(unsigned char *buf, unsigned int maxlen);

/* 主动关闭(ESTABLISHED/CLOSE_WAIT 均可调) */
void tcp_close(void);

/* 状态查询(命令行展示) */
unsigned int tcp_state(void);
const char  *tcp_state_name(void);
/* 本地端口(netinfo/tcp 命令展示;0=未绑定) */
unsigned short tcp_local_port(void);

/* net.c IP 分发调用:seg = TCP 报文(IP payload),src_ip = 对端 */
void tcp_handle(const unsigned char *seg, unsigned int len,
                unsigned int src_ip);

#endif /* NET_TCP_H */
