/* net/icmp.c - ICMP echo
 *
 * 只做 echo request/reply(ping)。同步单请求模型:同一时刻只有一个
 * 在途 ping,与 shell 的阻塞会话模型一致,无需请求表。
 * 校验和:发送侧用 RFC1071 算头;接收侧整包重算应为 0,不为 0 丢弃。
 */
#include "icmp.h"
#include "../mode/timer.h"

#define ICMP_TYPE_ECHO_REPLY 0u
#define ICMP_TYPE_ECHO_REQ   8u
#define ICMP_HDR_LEN         8u

/* ping 标识符:'Y''C' 固定;序号每次 +1 区分先后请求 */
#define PING_ID  0x5943u

static volatile int           ping_got;
static volatile unsigned int  ping_rtt_ticks;
static unsigned short         ping_seq_next;
static unsigned short         ping_seq_last;
static unsigned int           ping_start;
static unsigned int           ping_src;      /* 期待的 reply 源 IP */

void icmp_handle(const unsigned char *pkt, unsigned int len,
                 unsigned int src_ip)
{
    unsigned int sum;
    unsigned short id, seq;

    if (len < ICMP_HDR_LEN)
        return;
    if (pkt[0] != ICMP_TYPE_ECHO_REPLY || pkt[1] != 0)
        return;

    /* 整包(含校验和字段)重算,正确报文结果为 0 */
    sum = net_checksum(pkt, len);
    if (sum != 0)
        return;

    id  = (unsigned short)((pkt[4] << 8) | pkt[5]);
    seq = (unsigned short)((pkt[6] << 8) | pkt[7]);
    if (!ping_got && id == (PING_ID & 0xFFFFu)
        && seq == ping_seq_last && src_ip == ping_src) {
        ping_rtt_ticks = timer_ticks() - ping_start;
        ping_got = 1;
    }
}

int icmp_ping(unsigned int ip, unsigned int timeout_ticks,
              unsigned int *out_rtt)
{
    unsigned char pkt[ICMP_HDR_LEN + 8];   /* 头 + 8 字节 payload(利于排障) */
    unsigned int  sum;
    unsigned int  start;

    if (!net_ready())
        return -1;

    ping_seq_next++;
    ping_seq_last = ping_seq_next;
    ping_src      = ip;
    ping_got      = 0;

    /* type=8 code=0 cksum=? id=0x5943 seq=n + 8 字节 payload */
    pkt[0] = (unsigned char)ICMP_TYPE_ECHO_REQ;
    pkt[1] = 0;
    pkt[2] = 0;
    pkt[3] = 0;
    pkt[4] = (unsigned char)((PING_ID >> 8) & 0xFFu);
    pkt[5] = (unsigned char)(PING_ID & 0xFFu);
    pkt[6] = (unsigned char)(ping_seq_next >> 8);
    pkt[7] = (unsigned char)(ping_seq_next & 0xFFu);
    pkt[8]  = 'Y'; pkt[9]  = 'u'; pkt[10] = 'a'; pkt[11] = 'n';
    pkt[12] = 'C'; pkt[13] = 'o'; pkt[14] = 'r'; pkt[15] = 'e';

    sum = net_checksum(pkt, sizeof(pkt));
    pkt[2] = (unsigned char)(sum >> 8);
    pkt[3] = (unsigned char)(sum & 0xFFu);

    ping_start = timer_ticks();
    if (net_ip_send(IP_PROTO_ICMP, ip, pkt, sizeof(pkt)) != 0)
        return -1;

    start = ping_start;
    while (!ping_got) {
        net_poll_frame();
        if (timer_ticks() - start >= timeout_ticks) {
            if (out_rtt != 0)
                *out_rtt = 0;
            return -2;
        }
    }

    if (out_rtt != 0)
        *out_rtt = ping_rtt_ticks;
    return 0;
}
