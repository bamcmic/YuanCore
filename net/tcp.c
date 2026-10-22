/* net/tcp.c - 单连接阻塞式 TCP(单 TCB)
 *
 * 详见 tcp.h 头注。收发全部经 net_ip_send / net_poll_frame 轮询路径,
 * 不依赖网卡中断;e1000 中断化后本文件无需改动(等待条件不变)。
 */
#include "tcp.h"
#include "netcfg.h"

#include "../mode/timer.h"

#ifdef YC_X64
extern void ser_puts64(const char *s);
#define TCPLOG(s) ser_puts64(s)
#else
#define TCPLOG(s)
#endif

/* ---- TCB(全部静态) ---- */
static unsigned int  t_state;
static unsigned int  t_local_port;
static unsigned int  t_remote_port;
static unsigned int  t_remote_ip;
static unsigned int  t_iss;
static unsigned int  t_irs;       /* 对端初始序号(记录用) */
static unsigned int  t_snd_una;   /* 最老未确认序号 */
static unsigned int  t_snd_nxt;   /* 下一个要发的序号 */
static unsigned int  t_rcv_nxt;   /* 期望收到的序号 */
static unsigned int  t_snd_wnd;   /* 对端通告窗口(记录,不用于流控) */

/* 接收环形缓冲 */
static unsigned char t_rxbuf[TCP_RXBUF];
static volatile unsigned int t_rx_head;
static volatile unsigned int t_rx_tail;
static volatile unsigned int t_rx_fin;    /* 已收到 FIN */
static volatile int          t_reset;     /* 收到 RST */

/* 在途段(重传用):单段在途,只存一份 */
static unsigned char t_if_buf[TCP_MSS];
static unsigned int  t_if_len;
static unsigned int  t_if_seq;
static unsigned char t_if_flags;          /* 原段 flags(重传按原样重发) */
static unsigned int  t_if_ticks;          /* 该段上次发送的 tick */
static unsigned int  t_if_retries;

/* 32 位序号回绕比较 */
#define SEQ_LT(a, b)  ((int)((a) - (b)) <  0)
#define SEQ_LEQ(a, b) ((int)((a) - (b)) <= 0)
#define SEQ_GT(a, b)  ((int)((a) - (b)) >  0)

/* tick 等待(空轮询由 timer 中断 hlt 唤醒) */
static void tcp_wait(unsigned int n)
{
    unsigned int t0 = timer_ticks();

    while (timer_ticks() - t0 < n)
        __asm__ __volatile__("hlt");
}

/* ---- TCP 头(网络序布局,手工组包) ---- */
struct tcp_hdr_raw {
    unsigned char raw[20];
};

static unsigned short get16(const unsigned char *p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

static unsigned int get32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16)
         | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static void put16(unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/* 临时端口池(connect 用;避开 UDP 的 4096/123) */
static unsigned int t_ephemeral;

static unsigned short next_ephemeral(void)
{
    t_ephemeral++;
    if (t_ephemeral > 65000u)
        t_ephemeral = 40000u;
    return (unsigned short)t_ephemeral;
}

/* 发出一个 TCP 段(组头+伪头校验和+IP 出站)。
 * data 可为 0(纯头段:SYN/FIN/ACK/RST)。返回 0/负 */
static int tcp_send_seg(unsigned char flags, unsigned int seq,
                        const unsigned char *data, unsigned int len,
                        unsigned short src_port, unsigned short dst_port)
{
    static unsigned char seg[TCP_MSS + 40];
    unsigned int total = 20u + len;
    unsigned int i;
    unsigned char *h = seg;

    /* --- TCP 头 20B --- */
    put16(h + 0, src_port);
    put16(h + 2, dst_port);
    put32(h + 4, seq);
    put32(h + 8, t_rcv_nxt);              /* ACK 号 */
    h[12] = 0x50;                         /* 数据偏移 5(20B),保留 0 */
    h[13] = flags;
    put16(h + 14, 0xFFF0u);               /* 窗口:固定大窗口 65520 */
    put16(h + 16, 0);                     /* 校验和后填 */
    put16(h + 18, 0);                     /* 紧急指针 */

    for (i = 0; i < len; i++)
        seg[20 + i] = data[i];

    /* --- 伪头 + 段 连续拼出,算 RFC1071 --- */
    {
        static unsigned char csum_buf[12 + TCP_MSS + 20];
        unsigned short csum;

        csum_buf[0]  = (unsigned char)(netcfg_ip() >> 24);
        csum_buf[1]  = (unsigned char)(netcfg_ip() >> 16);
        csum_buf[2]  = (unsigned char)(netcfg_ip() >> 8);
        csum_buf[3]  = (unsigned char)netcfg_ip();
        csum_buf[4]  = (unsigned char)(t_remote_ip >> 24);
        csum_buf[5]  = (unsigned char)(t_remote_ip >> 16);
        csum_buf[6]  = (unsigned char)(t_remote_ip >> 8);
        csum_buf[7]  = (unsigned char)t_remote_ip;
        csum_buf[8]  = 0;
        csum_buf[9]  = IP_PROTO_TCP;
        put16(csum_buf + 10, (unsigned short)total);
        net_memcpy(csum_buf + 12, seg, total);
        csum = net_checksum(csum_buf, 12u + total);
        put16(h + 16, csum);
    }

    return net_ip_send(IP_PROTO_TCP, t_remote_ip, seg, total);
}

/* 在途段登记+首发 */
static int tcp_tx_inflight(unsigned char flags, const unsigned char *data,
                           unsigned int len)
{
    int rc;

    if (len > TCP_MSS)
        len = TCP_MSS;
    net_memcpy(t_if_buf, data, len);
    t_if_len   = len;
    t_if_seq   = t_snd_nxt;
    t_if_flags = flags;
    t_if_ticks = timer_ticks();
    t_if_retries = 0;

    rc = tcp_send_seg(flags, t_if_seq, t_if_buf, t_if_len,
                      (unsigned short)t_local_port,
                      (unsigned short)t_remote_port);
    if (rc == 0)
        t_snd_nxt += len;
    return rc;
}

/* 重传在途段(不推进 snd_nxt,按原 flags) */
static void tcp_retransmit(void)
{
    tcp_send_seg(t_if_flags, t_if_seq, t_if_buf, t_if_len,
                 (unsigned short)t_local_port,
                 (unsigned short)t_remote_port);
    t_if_ticks = timer_ticks();
    t_if_retries++;
}

/* ---- 接收环形缓冲 ---- */
static void rx_push(const unsigned char *p, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        unsigned int next = (t_rx_head + 1u) % TCP_RXBUF;

        if (next == t_rx_tail)
            return;                       /* 满:丢尾(对端窗口足够大,不会发生) */
        t_rxbuf[t_rx_head] = p[i];
        t_rx_head = next;
    }
}

/* ---- 主状态机:IP 分发入口 ---- */
void tcp_handle(const unsigned char *seg, unsigned int len,
                unsigned int src_ip)
{
    unsigned short sport, dport, flags;
    unsigned int seq, ack;
    unsigned int hdr, dlen;
    const unsigned char *payload;

    if (len < 20u)
        return;

    sport = get16(seg + 0);
    dport = get16(seg + 2);
    seq   = get32(seg + 4);
    ack   = get32(seg + 8);
    flags = seg[13];
    hdr   = (unsigned int)(seg[12] >> 4) * 4u;
    if (hdr < 20u || hdr > len)
        return;
    dlen  = len - hdr;
    payload = seg + hdr;

    /* 单 TCB:端口必须命中本连接(监听态 remote 未定,只看本地端口) */
    if (t_state == TCPST_CLOSED)
        return;
    if (dport != (unsigned short)t_local_port)
        return;
    if (t_state != TCPST_LISTEN && t_state != TCPST_SYN_RCVD
        && t_state != TCPST_SYN_SENT) {
        if (src_ip != t_remote_ip || sport != (unsigned short)t_remote_port)
            return;
    }

    if (flags & TCPF_RST) {
        t_reset = 1;
        t_state = TCPST_CLOSED;
        return;
    }

    if (t_state == TCPST_LISTEN) {
        if ((flags & TCPF_SYN) == 0)
            return;
        t_remote_ip   = src_ip;
        t_remote_port = sport;
        t_irs         = seq;
        /* dlen 通常为 0;slirp 的 SYN 可能带对齐尾字节,一并计入确认号 */
        t_rcv_nxt     = seq + dlen + 1u;
        t_state       = TCPST_SYN_RCVD;
        t_if_seq   = t_snd_nxt;
        t_if_len   = 0;
        t_if_flags = TCPF_SYN | TCPF_ACK;
        t_if_ticks = timer_ticks();
        t_if_retries = 0;
        tcp_send_seg(TCPF_SYN | TCPF_ACK, t_snd_nxt, 0, 0,
                     (unsigned short)t_local_port,
                     (unsigned short)t_remote_port);
        return;
    }

    if (t_state == TCPST_SYN_SENT) {
        if ((flags & (TCPF_SYN | TCPF_ACK)) == (TCPF_SYN | TCPF_ACK)) {
            t_irs       = seq;
            t_rcv_nxt   = seq + 1u;
            t_snd_una   = ack;
            t_snd_wnd   = get16(seg + 14);
            t_state     = TCPST_ESTABLISHED;
            tcp_send_seg(TCPF_ACK, t_snd_nxt, 0, 0,
                         (unsigned short)t_local_port,
                         (unsigned short)t_remote_port);
        }
        return;
    }

    if (t_state == TCPST_SYN_RCVD) {
        if ((flags & TCPF_ACK) != 0) {
            t_snd_una = ack;
            t_snd_wnd = get16(seg + 14);
            t_state   = TCPST_ESTABLISHED;
        }
        return;
    }

    /* ---- ESTABLISHED / FIN_WAIT / CLOSE_WAIT / LAST_ACK ---- */

    /* ACK 处理:推进 snd_una(单段在途,前进即确认在途段) */
    if ((flags & TCPF_ACK) != 0 && SEQ_GT(ack, t_snd_una))
        t_snd_una = ack;

    /* 数据 */
    if (dlen != 0 && t_state != TCPST_LAST_ACK) {
        /* 只收按序段;乱序丢弃(对端 RTO 重传) */
        if (seq == t_rcv_nxt) {
            rx_push(payload, dlen);
            t_rcv_nxt += dlen;
        }
        /* 立即 ACK(即使乱序也回当前 rcv_nxt,催促对端重传) */
        tcp_send_seg(TCPF_ACK, t_snd_nxt, 0, 0,
                     (unsigned short)t_local_port,
                     (unsigned short)t_remote_port);
    }

    /* FIN */
    if (flags & TCPF_FIN) {
        t_rcv_nxt = seq + dlen + 1u;      /* FIN 占一序号 */
        t_rx_fin = 1;
        tcp_send_seg(TCPF_ACK, t_snd_nxt, 0, 0,
                     (unsigned short)t_local_port,
                     (unsigned short)t_remote_port);
        if (t_state == TCPST_ESTABLISHED)
            t_state = TCPST_CLOSE_WAIT;   /* 被动关闭:等本侧 close */
        else if (t_state == TCPST_FIN_WAIT1)
            t_state = TCPST_CLOSING;
        else if (t_state == TCPST_FIN_WAIT2)
            t_state = TCPST_TIME_WAIT;
    }
}

/* ---- 对外 API ---- */

int tcp_init(void)
{
    t_state = TCPST_CLOSED;
    t_local_port = 0;
    t_remote_port = 0;
    t_remote_ip = 0;
    t_ephemeral = 40000u;
    t_rx_head = t_rx_tail = 0;
    t_rx_fin = 0;
    t_reset = 0;
    t_if_len = 0;
    return 0;
}

int tcp_connect(unsigned int ip, unsigned short port)
{
    unsigned int t0;

    if (!net_ready())
        return -1;
    if (t_state != TCPST_CLOSED)
        return -2;

    t_remote_ip   = ip;
    t_remote_port = port;
    t_local_port  = next_ephemeral();
    t_rx_head = t_rx_tail = 0;
    t_rx_fin = 0;
    t_reset = 0;
    t_iss       = timer_ticks() * 7919u + 12345u;   /* 粗 ISS */
    t_snd_una   = t_iss;
    t_snd_nxt   = t_iss;
    t_rcv_nxt   = 0;

    /* 发 SYN(在途段登记,空数据) */
    t_state = TCPST_SYN_SENT;
    if (tcp_tx_inflight(TCPF_SYN, 0, 0) != 0) {
        t_state = TCPST_CLOSED;
        return -3;
    }

    t0 = timer_ticks();
    while (t_state != TCPST_ESTABLISHED) {
        net_poll_frame();

        if (t_reset || t_state == TCPST_CLOSED) {
            t_state = TCPST_CLOSED;
            return -4;                    /* 收 RST */
        }
        if (timer_ticks() - t0 > TCP_CONNECT_TICKS)
            return -5;                    /* 总超时 */

        /* RTO:重传 SYN */
        if (timer_ticks() - t_if_ticks > TCP_RTO_TICKS) {
            if (t_if_retries >= TCP_MAX_RETRIES)
                return -6;
            tcp_retransmit();
        }
        tcp_wait(1);
    }
    return 0;
}

int tcp_listen(unsigned short port)
{
    unsigned int t0;

    if (!net_ready())
        return -1;
    if (t_state != TCPST_CLOSED)
        return -2;

    t_local_port = port;
    t_remote_ip = 0;
    t_remote_port = 0;
    t_rx_head = t_rx_tail = 0;
    t_rx_fin = 0;
    t_reset = 0;
    t_iss       = timer_ticks() * 7919u + 54321u;
    t_snd_una   = t_iss;
    t_snd_nxt   = t_iss;
    t_rcv_nxt   = 0;
    t_if_ticks  = timer_ticks();
    t_state = TCPST_LISTEN;

    t0 = timer_ticks();
    while (t_state != TCPST_ESTABLISHED) {
        net_poll_frame();

        if (t_reset || t_state == TCPST_CLOSED)
            return -3;
        if (timer_ticks() - t0 > TCP_CONNECT_TICKS) {
            t_state = TCPST_CLOSED;       /* 无人来连,回 CLOSED */
            return -4;
        }
        /* SYN_RCVD 阶段的 SYN+ACK 重传 */
        if (t_state == TCPST_SYN_RCVD
            && timer_ticks() - t_if_ticks > TCP_RTO_TICKS) {
            if (t_if_retries >= TCP_MAX_RETRIES)
                return -5;
            tcp_send_seg(TCPF_SYN | TCPF_ACK, t_snd_nxt, 0, 0,
                         (unsigned short)t_local_port,
                         (unsigned short)t_remote_port);
            t_if_ticks = timer_ticks();
            t_if_retries++;
        }
        tcp_wait(1);
    }
    return 0;
}

int tcp_send(const unsigned char *data, unsigned int len)
{
    unsigned int sent = 0;

    if (t_state != TCPST_ESTABLISHED)
        return -1;

    while (sent < len) {
        unsigned int n = len - sent;
        unsigned int t0;

        if (n > TCP_MSS)
            n = TCP_MSS;

        if (tcp_tx_inflight(TCPF_ACK | TCPF_PSH, data + sent, n) != 0)
            return (sent > 0) ? (int)sent : -2;

        t0 = timer_ticks();
        while (SEQ_GT((t_if_seq + t_if_len), t_snd_una)) {
            net_poll_frame();

            if (t_reset || t_state == TCPST_CLOSED)
                return (sent > 0) ? (int)sent : -3;
            if (t_state != TCPST_ESTABLISHED)
                return (sent > 0) ? (int)sent : -4;

            if (timer_ticks() - t0 > TCP_CONNECT_TICKS)
                return (sent > 0) ? (int)sent : -5;

            if (timer_ticks() - t_if_ticks > TCP_RTO_TICKS) {
                if (t_if_retries >= TCP_MAX_RETRIES)
                    return (sent > 0) ? (int)sent : -6;
                tcp_retransmit();
            }
            tcp_wait(1);
        }
        sent += n;
    }
    return (int)sent;
}

int tcp_recv(unsigned char *buf, unsigned int maxlen)
{
    unsigned int got = 0;
    unsigned int t0;

    if (t_state == TCPST_CLOSED)
        return -1;

    t0 = timer_ticks();
    while (got == 0) {
        net_poll_frame();

        if (t_reset)
            return -2;

        /* 取空缓冲 */
        while (t_rx_tail != t_rx_head && got < maxlen) {
            buf[got++] = t_rxbuf[t_rx_tail];
            t_rx_tail = (t_rx_tail + 1u) % TCP_RXBUF;
        }
        if (got != 0)
            break;

        /* 无数据:对端已 FIN 且缓冲空 -> EOF */
        if (t_rx_fin && t_state != TCPST_ESTABLISHED)
            return 0;
        if (t_state == TCPST_CLOSED)
            return 0;

        if (timer_ticks() - t0 > TCP_CONNECT_TICKS * 3u)
            return -3;                    /* 30s 无数据 */
        tcp_wait(1);
    }
    return (int)got;
}

void tcp_close(void)
{
    unsigned int t0;
    unsigned int st;

    if (t_state == TCPST_CLOSED || t_state == TCPST_LISTEN) {
        t_state = TCPST_CLOSED;
        return;
    }
    if (t_state == TCPST_SYN_SENT || t_state == TCPST_SYN_RCVD) {
        t_state = TCPST_CLOSED;           /* 握手未完成:静默放弃 */
        return;
    }

    /* 主动 FIN */
    t_state = TCPST_FIN_WAIT1;
    t_if_len   = 0;
    t_if_seq   = t_snd_nxt;
    t_if_ticks = timer_ticks();
    t_if_retries = 0;
    tcp_send_seg(TCPF_FIN | TCPF_ACK, t_snd_nxt, 0, 0,
                 (unsigned short)t_local_port,
                 (unsigned short)t_remote_port);
    t_snd_nxt += 1u;

    t0 = timer_ticks();
    while (t_state != TCPST_TIME_WAIT && t_state != TCPST_CLOSED) {
        net_poll_frame();
        if (t_reset) {
            t_state = TCPST_CLOSED;
            return;
        }
        if (timer_ticks() - t0 > TCP_CLOSE_TICKS) {
            t_state = TCPST_CLOSED;       /* 对端不应答:放弃 */
            return;
        }
        if (timer_ticks() - t_if_ticks > TCP_RTO_TICKS
            && t_state == TCPST_FIN_WAIT1) {
            if (t_if_retries >= TCP_MAX_RETRIES) {
                t_state = TCPST_CLOSED;
                return;
            }
            tcp_send_seg(TCPF_FIN | TCPF_ACK, t_if_seq, 0, 0,
                         (unsigned short)t_local_port,
                         (unsigned short)t_remote_port);
            t_if_ticks = timer_ticks();
            t_if_retries++;
        }
        tcp_wait(1);
    }

    /* TIME_WAIT:跳过 2MSL(单连接本地环境无旧段风险) */
    st = t_state;
    (void)st;
    t_state = TCPST_CLOSED;
}

unsigned int tcp_state(void)
{
    return t_state;
}

const char *tcp_state_name(void)
{
    switch (t_state) {
    case TCPST_LISTEN:      return "LISTEN";
    case TCPST_SYN_SENT:    return "SYN_SENT";
    case TCPST_SYN_RCVD:    return "SYN_RCVD";
    case TCPST_ESTABLISHED: return "ESTABLISHED";
    case TCPST_FIN_WAIT1:   return "FIN_WAIT1";
    case TCPST_FIN_WAIT2:   return "FIN_WAIT2";
    case TCPST_CLOSE_WAIT:  return "CLOSE_WAIT";
    case TCPST_LAST_ACK:    return "LAST_ACK";
    case TCPST_CLOSING:     return "CLOSING";
    case TCPST_TIME_WAIT:   return "TIME_WAIT";
    default:                return "CLOSED";
    }
}

unsigned short tcp_local_port(void)
{
    return (unsigned short)t_local_port;
}
