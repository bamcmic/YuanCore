/* net/netcfg.c - 网络配置状态 + /net.cfg 持久化
 *
 * 纯数据模块:不依赖 net.h/net.c(避免环),只碰 mode/fs。
 * 全部值按主机序保存;文件格式见 netcfg.h 头注释。
 */
#include "netcfg.h"
#include "../mode/fs.h"

static unsigned int cfg_ip    = 0x0A00020Fu;   /* 10.0.2.15      */
static unsigned int cfg_mask  = 0xFFFFFF00u;   /* 255.255.255.0  */
static unsigned int cfg_gw    = 0x0A000202u;   /* 10.0.2.2       */
static unsigned int cfg_dns   = 0x72727272u;   /* 114.114.114.114
   注:slirp 的内置 DNS(10.0.2.3) 在部分 QEMU 版本/宿主环境下不回包
   (2026-10 WSL2+QEMU 8.2.2 实测),公网 DNS 经 NAT 转发更可靠 */
static unsigned int cfg_ntp   = 0x78197314u;   /* 120.25.115.20(阿里云)
   注:203.107.6.102 在部分网络下 UDP:123 不通(实测),换此台 */

void netcfg_defaults(void)
{
    cfg_ip   = 0x0A00020Fu;
    cfg_mask = 0xFFFFFF00u;
    cfg_gw   = 0x0A000202u;
    cfg_dns  = 0x72727272u;   /* 114.114.114.114 */
    cfg_ntp  = 0x78197314u;   /* 120.25.115.20  */
}

/* ---- 点分十进制 ---- */

int net_parse_ip(const char *s, unsigned int *out)
{
    unsigned int v = 0;
    int octet, digits;

    if (s == 0)
        return -1;

    for (octet = 0; octet < 4; octet++) {
        digits = 0;
        v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (unsigned int)(*s - '0');
            if (v > 255u)
                return -1;
            s++;
            digits++;
            if (digits > 3)
                return -1;
        }
        if (digits == 0)
            return -1;

        /* 第一段 = 最高字节:"10.0.2.15" -> 0x0A00020F(主机序) */
        if (octet == 0)
            *out = v << 24;
        else
            *out |= v << (24u - 8u * (unsigned int)octet);

        if (octet < 3) {
            if (*s != '.')
                return -1;
            s++;
        }
    }
    return (*s == '\0') ? 0 : -1;
}

void net_format_ip(unsigned int ip, char out[16])
{
    int i = 0, seg;

    /* 最高字节先输出:0x0A00020F -> "10.0.2.15" */
    for (seg = 3; seg >= 0; seg--) {
        unsigned int o = (ip >> (seg * 8)) & 0xFFu;
        char tmp[4];
        int  n = 0, k;

        if (seg != 3)
            out[i++] = '.';
        if (o == 0)
            tmp[n++] = '0';
        while (o > 0) {
            tmp[n++] = (char)('0' + (o % 10u));
            o /= 10u;
        }
        for (k = n - 1; k >= 0; k--)
            out[i++] = tmp[k];
    }
    out[i] = '\0';
}

/* ---- getter ---- */

unsigned int netcfg_ip(void)      { return cfg_ip;   }
unsigned int netcfg_netmask(void) { return cfg_mask; }
unsigned int netcfg_gateway(void) { return cfg_gw;   }
unsigned int netcfg_dns(void)     { return cfg_dns;  }
unsigned int netcfg_ntp(void)     { return cfg_ntp;  }

/* ---- setter(parse -> 更新 -> 落盘) ---- */

static int set_one(const char *dotted, unsigned int *slot)
{
    unsigned int v;

    if (net_parse_ip(dotted, &v) != 0)
        return -1;
    *slot = v;
    return netcfg_save();
}

int netcfg_set_ip(const char *dotted)   { return set_one(dotted, &cfg_ip);   }
int netcfg_set_mask(const char *dotted) { return set_one(dotted, &cfg_mask); }
int netcfg_set_gw(const char *dotted)   { return set_one(dotted, &cfg_gw);   }
int netcfg_set_dns(const char *dotted)  { return set_one(dotted, &cfg_dns);  }
int netcfg_set_ntp(const char *dotted)  { return set_one(dotted, &cfg_ntp);  }

/* ---- 持久化 ---- */

int netcfg_save(void)
{
    char buf[160];
    int  n;
    char a[16], b[16], c[16], d[16], e[16];
    const char *pa, *pb, *pc, *pd, *pe;

    net_format_ip(cfg_ip, a);
    net_format_ip(cfg_mask, b);
    net_format_ip(cfg_gw, c);
    net_format_ip(cfg_dns, d);
    net_format_ip(cfg_ntp, e);
    pa = a; pb = b; pc = c; pd = d; pe = e;

    n = 0;
    buf[n] = 'i'; buf[n+1] = 'p'; buf[n+2] = '='; n += 3;
    while (*pa) buf[n++] = *pa++;
    buf[n++] = '\n';
    buf[n] = 'm'; buf[n+1] = 'a'; buf[n+2] = 's'; buf[n+3] = 'k'; buf[n+4] = '='; n += 5;
    while (*pb) buf[n++] = *pb++;
    buf[n++] = '\n';
    buf[n] = 'g'; buf[n+1] = 'w'; buf[n+2] = '='; n += 3;
    while (*pc) buf[n++] = *pc++;
    buf[n++] = '\n';
    buf[n] = 'd'; buf[n+1] = 'n'; buf[n+2] = 's'; buf[n+3] = '='; n += 4;
    while (*pd) buf[n++] = *pd++;
    buf[n++] = '\n';
    buf[n] = 'n'; buf[n+1] = 't'; buf[n+2] = 'p'; buf[n+3] = '='; n += 4;
    while (*pe) buf[n++] = *pe++;
    buf[n++] = '\n';
    buf[n] = '\0';

    if (fs_write(NETCFG_PATH, buf, n) < 0)
        return -1;
    return 0;
}

int netcfg_load(void)
{
    char buf[256];
    int  n, i, line_start;

    if (!fs_exists(NETCFG_PATH))
        return 0;
    n = fs_read(NETCFG_PATH, buf, (int)sizeof(buf) - 1);
    if (n <= 0)
        return 0;
    buf[n] = '\0';

    /* 逐行: key=value;key 必须全匹配,value 点分十进制,坏行跳过 */
    line_start = 0;
    for (i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            buf[i] = '\0';
            if (i > line_start) {
                char *eq = 0;
                int   j;

                for (j = line_start; j < i; j++) {
                    if (buf[j] == '=') {
                        eq = &buf[j];
                        break;
                    }
                }
                if (eq != 0) {
                    unsigned int v;
                    char        *key = &buf[line_start];
                    char        *val = eq + 1;

                    *eq = '\0';
                    if (net_parse_ip(val, &v) == 0) {
                        /* key 匹配(小写,无前导空格容错) */
                        if (key[0] == 'i' && key[1] == 'p' && key[2] == '\0')
                            cfg_ip = v;
                        else if (key[0] == 'm' && key[1] == 'a' && key[2] == 's'
                                 && key[3] == 'k' && key[4] == '\0')
                            cfg_mask = v;
                        else if (key[0] == 'g' && key[1] == 'w' && key[2] == '\0')
                            cfg_gw = v;
                        else if (key[0] == 'd' && key[1] == 'n' && key[2] == 's'
                                 && key[3] == '\0')
                            cfg_dns = v;
                        else if (key[0] == 'n' && key[1] == 't' && key[2] == 'p'
                                 && key[3] == '\0')
                            cfg_ntp = v;
                    }
                }
            }
            line_start = i + 1;
        }
    }
    return 1;
}
