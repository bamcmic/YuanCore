/* net/dns.c - 最小 DNS 客户端(UDP:53,A 记录)
 *
 * 协议面:标准递归查询(RD=1),QEMU user-net 的内置 DNS(10.0.2.3)
 * 代理到宿主上游。应答解析覆盖:压缩指针(0xC0xx)与内联标签两种
 * owner name;只认 IN/A,其余 answer 跳过。单条缓存(name 忽略大小写)。
 * 同步阻塞,3 秒超时,重发一次 —— 与 ntp/icmp 同款会话模型。
 */
#include "dns.h"
#include "netcfg.h"
#include "../mode/timer.h"

#define DNS_PORT       53u
#define DNS_SRC_PORT   4096u        /* 源端口不用 53:部分 resolver 拒收防欺骗 */
#define DNS_HDR_LEN    12u
#define DNS_TYPE_A     1u
#define DNS_TIMEOUT    300u          /* 3s @ 100Hz */
#define DNS_NAME_MAX   100u

static volatile int           dns_got;
static volatile unsigned int  dns_answer_ip;
static unsigned short         dns_txid;
static char                   dns_cache_name[DNS_NAME_MAX];
static unsigned int           dns_cache_ip;
static int                    dns_cache_valid;
static int                    dns_listened;   /* udp:53 只注册一次 */

/* 大小写不敏感比较(-nostdlib 无 strcasecmp) */
static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;

        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb)
            return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void dns_udp_cb(const unsigned char *payload, unsigned int len,
                       unsigned int src_ip, unsigned short src_port)
{
    unsigned int off;
    unsigned int ancount, i;
    unsigned short flags;

    (void)src_ip;
    (void)src_port;

    if (len < DNS_HDR_LEN + 4u)
        return;
    if (ntohs16(*(const unsigned short *)(payload + 0)) != dns_txid)
        return;                                /* 不是我们的应答 */

    flags   = ntohs16(*(const unsigned short *)(payload + 2));
    ancount = ntohs16(*(const unsigned short *)(payload + 6));
    if ((flags & 0x000Fu) != 0)                /* RCODE != 0 */
        return;
    if (ancount == 0)
        return;

    /* 跳过 question 区:QNAME(标签链到 0) + qtype(2) + qclass(2) */
    off = DNS_HDR_LEN;
    while (off < len && payload[off] != 0)
        off += (unsigned int)payload[off] + 1u;
    off += 1u + 4u;

    /* answer 区:owner(指针或标签) type class ttl rdlen rdata */
    for (i = 0; i < ancount && off + 10u <= len; i++) {
        unsigned int type, rdlen;

        if ((payload[off] & 0xC0u) == 0xC0u)
            off += 2u;
        else {
            while (off < len && payload[off] != 0)
                off += (unsigned int)payload[off] + 1u;
            off += 1u;
        }
        if (off + 10u > len)
            return;

        type   = ntohs16(*(const unsigned short *)(payload + off));
        rdlen  = ntohs16(*(const unsigned short *)(payload + off + 8));
        off   += 10u;                        /* type+class+ttl(4) */
        if (off + rdlen > len)
            return;

        if (type == DNS_TYPE_A && rdlen == 4u && !dns_got) {
            /* rdata 网络序:首字节 = 地址最高字节 */
            dns_answer_ip = ((unsigned int)payload[off] << 24)
                          | ((unsigned int)payload[off + 1] << 16)
                          | ((unsigned int)payload[off + 2] << 8)
                          |  (unsigned int)payload[off + 3];
            dns_got = 1;
            return;
        }
        off += rdlen;
    }
}

/* 名字 -> QNAME 编码("www.a.com" -> 3www1a3com0),返回编码长度,-1 失败 */
static int encode_qname(const char *name, unsigned char *out, unsigned int max)
{
    unsigned int p = 0;

    while (*name) {
        unsigned int seg = 0;
        const char  *s   = name;

        while (s[seg] && s[seg] != '.')
            seg++;
        if (seg == 0 || seg > 63u || p + 1u + seg > max - 1u)
            return -1;
        out[p++] = (unsigned char)seg;
        for (; seg > 0; seg--)
            out[p++] = (unsigned char)*name++;
        if (*name == '.')
            name++;
    }
    if (p == 0)
        return -1;
    out[p++] = 0;
    return (int)p;
}

int dns_resolve(const char *name, unsigned int *out_ip)
{
    unsigned char pkt[DNS_HDR_LEN + DNS_NAME_MAX + 4u];
    unsigned int  server;
    unsigned int  start, tries, qnamelen;
    int           n;

    if (!net_ready())
        return -1;
    if (name == 0 || out_ip == 0)
        return -1;

    /* 单条缓存 */
    if (dns_cache_valid && name_eq(name, dns_cache_name)) {
        *out_ip = dns_cache_ip;
        return 0;
    }

    server = netcfg_dns();
    if (server == 0)
        return -1;

    if (!dns_listened) {
        udp_listen((unsigned short)DNS_PORT, dns_udp_cb);
        dns_listened = 1;
    }

    /* 组 query:header + qname + qtype=A + qclass=IN */
    dns_txid = (unsigned short)((timer_ticks() * 2654435761u) >> 16);
    if (dns_txid == 0)
        dns_txid = 1;

    pkt[0]  = (unsigned char)(dns_txid >> 8);
    pkt[1]  = (unsigned char)(dns_txid & 0xFFu);
    pkt[2]  = 0x01;                            /* RD=1 递归查询 */
    pkt[3]  = 0x00;
    pkt[4]  = 0; pkt[5]  = 1;                  /* qdcount = 1 */
    pkt[6]  = 0; pkt[7]  = 0;                  /* ancount = 0 */
    pkt[8]  = 0; pkt[9]  = 0;                  /* nscount = 0 */
    pkt[10] = 0; pkt[11] = 0;                  /* arcount = 0 */

    n = encode_qname(name, pkt + DNS_HDR_LEN,
                     sizeof(pkt) - DNS_HDR_LEN - 4u);
    if (n < 0)
        return -1;
    qnamelen = (unsigned int)n;
    pkt[DNS_HDR_LEN + qnamelen]      = 0;      /* qtype = A */
    pkt[DNS_HDR_LEN + qnamelen + 1]  = DNS_TYPE_A;
    pkt[DNS_HDR_LEN + qnamelen + 2]  = 0;     /* qclass = IN */
    pkt[DNS_HDR_LEN + qnamelen + 3]  = 1;

    /* 两次机会:各 3 秒 */
    for (tries = 0; tries < 2; tries++) {
        dns_got = 0;
        if (udp_send(server, (unsigned short)DNS_PORT,
                     (unsigned short)DNS_SRC_PORT, pkt,
                     DNS_HDR_LEN + qnamelen + 4u) != 0)
            return -1;

        start = timer_ticks();
        while (!dns_got) {
            net_poll_frame();
            if (timer_ticks() - start >= DNS_TIMEOUT)
                break;
        }
        if (dns_got)
            break;
    }

    if (!dns_got)
        return -2;

    /* 更新缓存 */
    {
        unsigned int k = 0;

        while (name[k] && k < sizeof(dns_cache_name) - 1u) {
            dns_cache_name[k] = name[k];
            k++;
        }
        dns_cache_name[k] = '\0';
    }
    dns_cache_ip    = dns_answer_ip;
    dns_cache_valid = 1;
    *out_ip         = dns_answer_ip;
    return 0;
}

int net_resolve(const char *host, unsigned int *out_ip)
{
    if (host == 0 || out_ip == 0)
        return -1;

    /* 点分十进制直通 */
    if (net_parse_ip(host, out_ip) == 0)
        return 0;

    return dns_resolve(host, out_ip);
}
