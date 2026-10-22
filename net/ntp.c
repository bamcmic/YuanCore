/* net/ntp.c - NTP 客户端:从网络同步时间
 *
 * 流程(shell 命令 ntpsync 触发,同步阻塞):
 *   1. 注册 UDP:123 回调
 *   2. 发 48 字节 NTP client 请求(VN=4, Mode=3)到服务器
 *   3. 轮询 net_poll_frame 直到收到 reply 或超时(5s)
 *   4. 取服务器 Transmit Timestamp(offset 40,大端 64 位,取秒部分)
 *   5. NTP 纪元(1900) -> Unix 纪元(1970) -> +8h 北京时间 -> 写 CMOS
 *
 * 服务器 IP 由调用方传入(cmd 层先 net_resolve,支持域名/点分),
 * 默认值来自 /net.cfg 的 ntp= 项。
 *
 * 32 位运算边界:NTP 秒(0..~4.29e9)减 2208988800 得 Unix 秒,
 * 全程 unsigned int,2106 年前不会溢出。
 */
#include "net.h"
#include "../mode/timer.h"
#include "../mode/rtc.h"

#ifdef YC_X64
extern void ser_puts64(const char *s);
#define NETLOG(s) ser_puts64(s)
#else
#define NETLOG(s)
#endif

/* 收到的服务器时间(回调写入) */
static volatile int     ntp_got_reply;
static volatile unsigned int ntp_rx_sec;    /* 网络序读出已转主机序的秒 */

static void ntp_udp_cb(const unsigned char *payload, unsigned int len,
                       unsigned int src_ip, unsigned short src_port)
{
    (void)src_ip;
    (void)src_port;

    /* NTP 报文至少 48 字节; Transmit Timestamp 在 offset 40..43(秒) */
    if (len < 48u)
        return;

    ntp_rx_sec = ntohl32(*(const unsigned int *)(payload + 40));
    ntp_got_reply = 1;
}

/* days_since_epoch -> y/m/d(Hinnant civil_from_days,全 32 位无符号;
 * 1970+ 年代 z 恒为正,无需处理负数分支) */
static void civil_from_days(unsigned int z, int *y, int *m, int *d)
{
    unsigned int era, doe, yoe, doy, mp;

    z += 719468u;                         /* 0000-03-01 起的天数 */
    era = z / 146097u;                    /* 400 年一个 era */
    doe = z - era * 146097u;
    yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    {
        unsigned int yy = yoe + era * 400u;

        doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
        mp  = (5u * doy + 2u) / 153u;
        *d  = (int)(doy - (153u * mp + 2u) / 5u + 1u);
        *m  = (int)((mp < 10u) ? mp + 3u : mp - 9u);
        if (*m <= 2)
            yy += 1u;
        *y = (int)yy;
    }
}

/* unix 秒(已含时区偏移) -> rtc_time */
static void unix_to_rtc(unsigned int t, struct rtc_time *out)
{
    unsigned int days = t / 86400u;
    unsigned int secs = t % 86400u;

    civil_from_days(days, &out->year, &out->month, &out->day);
    out->hour = (int)(secs / 3600u);
    out->min  = (int)((secs % 3600u) / 60u);
    out->sec  = (int)(secs % 60u);
}

int ntp_sync(unsigned int server_ip, unsigned int *out_unix, int out_hms[3])
{
    unsigned char pkt[48];
    unsigned char junk_mac[6];
    unsigned int start;
    unsigned int unix_sec, local;
    struct rtc_time rt;

    if (!net_ready())
        return -1;

    ntp_got_reply = 0;
    udp_listen((unsigned short)NTP_PORT, ntp_udp_cb);

    /* 构造 client 请求:LI=0 VN=4 Mode=3 -> 0b00_100_011 = 0x23 */
    net_memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x23;

    /* 常规第一次握手要 ARP(同网段解析服务器,跨网段解析网关);
     * udp_send 内部会做,这里先显式解析一遍,失败立刻给用户明确
     * 报错,不混进 NTP 超时里 */
    if (net_route_peer_mac(server_ip, junk_mac) != 0)
        return -2;

    NETLOG(" NTP>TX");
    if (udp_send(server_ip, (unsigned short)NTP_PORT,
                 (unsigned short)43125u, pkt, sizeof(pkt)) != 0)
        return -3;

    /* 轮询等 reply:5 秒(timer 100Hz -> 500 tick) */
    start = timer_ticks();
    while (!ntp_got_reply) {
        net_poll_frame();
        if (timer_ticks() - start >= 500)
            return -4;
    }
    NETLOG(" NTP<RX");

    if (ntp_rx_sec < NTP_EPOCH_DELTA)
        return -5;                        /* 服务器时间不合理 */
    unix_sec = ntp_rx_sec - NTP_EPOCH_DELTA;

    /* 北京时间 = UTC + 8h,写进 CMOS(桌面时钟/date 一直按本地墙钟显示) */
    local = unix_sec + TZ_OFFSET_SEC;
    unix_to_rtc(local, &rt);
    if (rtc_write(&rt) != 0)
        return -6;

    if (out_unix != 0)
        *out_unix = unix_sec;
    if (out_hms != 0) {
        out_hms[0] = rt.hour;
        out_hms[1] = rt.min;
        out_hms[2] = rt.sec;
    }
    return 0;
}
