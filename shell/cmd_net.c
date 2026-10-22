/* shell/cmd_net.c - 网络命令:netinfo / net / ping / dns / ntpsync
 *
 * netinfo : 展示网卡(PCI/驱动/MAC)与当前配置(IP/掩码/网关/DNS/NTP)
 * net set : 运行时改配置(net set ip 10.0.2.15),自动持久化 /net.cfg
 * ping    : ICMP echo,支持点分 IP 与域名
 * dns     : 单次域名解析
 * ntpsync : NTP 对时(可选服务器参数),写入 CMOS RTC
 */
#include "shell.h"
#include "console.h"

#include "../net/net.h"
#include "../net/netcfg.h"
#include "../net/dns.h"
#include "../net/icmp.h"
#include "../net/tcp.h"
#include "../mode/rtc.h"

/* ---- 输出辅助 ---- */

static void put_dec(unsigned int v)
{
    char tmp[12];
    int i = 0;

    if (v == 0) {
        console_putc('0');
        return;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        console_putc(tmp[--i]);
}

static void put_hex32(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    char tmp[9];
    int i;

    tmp[8] = '\0';
    for (i = 7; i >= 0; i--) {
        tmp[i] = d[v & 0xF];
        v >>= 4;
    }
    console_puts("0x");
    console_puts(tmp);
}

static void put_mac(const unsigned char mac[6])
{
    int i;

    for (i = 0; i < 6; i++) {
        const char *d = "0123456789ABCDEF";

        if (i > 0)
            console_putc(':');
        console_putc(d[(mac[i] >> 4) & 0xF]);
        console_putc(d[mac[i] & 0xF]);
    }
}

/* dotted decimal,主机序 IP */
static void put_ip(unsigned int ip)
{
    char buf[16];

    net_format_ip(ip, buf);
    console_puts(buf);
}

static void put_rtc_hms(const struct rtc_time *t)
{
    if (t->hour < 10) console_putc('0');
    put_dec((unsigned int)t->hour);  console_putc(':');
    if (t->min < 10) console_putc('0');
    put_dec((unsigned int)t->min);   console_putc(':');
    if (t->sec < 10) console_putc('0');
    put_dec((unsigned int)t->sec);
}

/* ---- 分词:从 args 取下一个 token(空格分隔),返回长度,-1=没了 ---- */

static int next_tok(const char **p, const char **tok)
{
    const char *s = *p;

    while (*s == ' ')
        s++;
    if (*s == '\0')
        return -1;
    *tok = s;
    while (*s && *s != ' ')
        s++;
    {
        int len = (int)(s - *tok);

        *p = s;
        return len;
    }
}

static int tok_eq(const char *tok, int len, const char *lit)
{
    int i;

    for (i = 0; i < len; i++) {
        if (lit[i] == '\0' || tok[i] != lit[i])
            return 0;
    }
    return lit[len] == '\0';
}

/* ---- netinfo ---- */

static void print_config(void)
{
    console_puts("IP       : ");
    put_ip(netcfg_ip());
    console_puts("  mask ");
    put_ip(netcfg_netmask());
    console_puts("\nGateway  : ");
    put_ip(netcfg_gateway());
    console_puts("\nDNS      : ");
    put_ip(netcfg_dns());
    console_puts("\nNTP      : ");
    put_ip(netcfg_ntp());
    console_puts(":123\n");
}

static void cmd_netinfo(const char *args)
{
    unsigned char mac[6];

    (void)args;
    if (!net_ready()) {
        console_puts("[err] network not present "
                     "(no e1000 PCI device / init failed)\n");
        return;
    }

    console_puts("NIC      : Intel 82540EM (e1000) @ PCI BAR0 ");
    put_hex32(net_pci_bar0());
    console_puts(" irq ");
    put_dec((unsigned int)net_pci_irq_line());
    console_puts("\nMAC      : ");
    net_get_mac(mac);
    put_mac(mac);
    console_puts("\n");
    print_config();
    console_puts("Mode     : polling RX (no IRQ yet)\n");
}

/* ---- net set ---- */

static void cmd_net(const char *args)
{
    const char *p = args, *t1 = 0, *t2 = 0, *t3 = 0;
    int l1, l2, l3;

    l1 = next_tok(&p, &t1);
    if (l1 < 0) {                       /* 无参:显示配置 */
        if (!net_ready()) {
            console_puts("[err] network not present\n");
            return;
        }
        print_config();
        return;
    }
    if (!tok_eq(t1, l1, "set")) {
        console_puts("usage: net set <ip|mask|gw|dns|ntp> <a.b.c.d>\n");
        return;
    }
    l2 = next_tok(&p, &t2);
    l3 = next_tok(&p, &t3);
    if (l2 < 0 || l3 < 0) {
        console_puts("usage: net set <ip|mask|gw|dns|ntp> <a.b.c.d>\n");
        return;
    }
    if (l3 >= 16) {
        console_puts("[err] bad address\n");
        return;
    }
    {
        char val[16];
        int  i, rc = -1;

        for (i = 0; i < l3; i++)
            val[i] = t3[i];
        val[l3] = '\0';

        if      (tok_eq(t2, l2, "ip"))   rc = netcfg_set_ip(val);
        else if (tok_eq(t2, l2, "mask")) rc = netcfg_set_mask(val);
        else if (tok_eq(t2, l2, "gw"))   rc = netcfg_set_gw(val);
        else if (tok_eq(t2, l2, "dns"))  rc = netcfg_set_dns(val);
        else if (tok_eq(t2, l2, "ntp"))  rc = netcfg_set_ntp(val);
        else {
            console_puts("[err] unknown key (ip/mask/gw/dns/ntp)\n");
            return;
        }

        if (rc != 0) {
            console_puts("[err] set failed "
                         "(bad address or fs write)\n");
            return;
        }
        console_puts("  [ok ] saved to /net.cfg: ");
        print_config();
    }
}

/* ---- ping ---- */

static void cmd_ping(const char *args)
{
    const char *p = args, *t;
    int l, i;
    unsigned int ip;
    unsigned int sent = 0, got = 0;
    unsigned int tmin = 0xFFFFFFFFu, tmax = 0;

    l = next_tok(&p, &t);
    if (l < 0) {
        console_puts("usage: ping <ip|host>\n");
        return;
    }
    if (!net_ready()) {
        console_puts("[err] network not present\n");
        return;
    }
    {
        char host[64];

        if (l >= (int)sizeof(host)) {
            console_puts("[err] name too long\n");
            return;
        }
        for (i = 0; i < l; i++)
            host[i] = t[i];
        host[l] = '\0';

        if (net_resolve(host, &ip) != 0) {
            console_puts("[err] cannot resolve '");
            console_puts(host);
            console_puts("'\n");
            return;
        }
    }

    console_puts("PING ");
    put_ip(ip);
    console_puts(" 4 packets\n");

    for (i = 0; i < 4; i++) {
        unsigned int rtt = 0;
        int rc = icmp_ping(ip, 100 /* 1s */, &rtt);

        sent++;
        if (rc == 0) {
            unsigned int ms = rtt * 10u;

            got++;
            if (rtt < tmin) tmin = rtt;
            if (rtt > tmax) tmax = rtt;
            console_puts("  reply seq=");
            put_dec((unsigned int)i);
            console_puts(" time=");
            put_dec(ms);
            console_puts("ms (");
            put_dec(rtt);
            console_puts(" ticks)\n");
        } else {
            console_puts("  request timeout (seq=");
            put_dec((unsigned int)i);
            console_puts(")\n");
        }
    }

    console_puts("--- ");
    put_dec(got);
    console_puts("/");
    put_dec(sent);
    console_puts(" replies");
    if (got > 0) {
        console_puts(", rtt ");
        put_dec(tmin * 10u);
        console_puts("-");
        put_dec(tmax * 10u);
        console_puts("ms");
    }
    console_puts("\n");
}

/* ---- dns ---- */

static void cmd_dns(const char *args)
{
    const char *p = args, *t;
    int l, i;
    unsigned int ip;
    char host[64];

    l = next_tok(&p, &t);
    if (l < 0) {
        console_puts("usage: dns <name>\n");
        return;
    }
    if (!net_ready()) {
        console_puts("[err] network not present\n");
        return;
    }
    if (l >= (int)sizeof(host)) {
        console_puts("[err] name too long\n");
        return;
    }
    for (i = 0; i < l; i++)
        host[i] = t[i];
    host[l] = '\0';

    if (net_resolve(host, &ip) != 0) {
        console_puts("[err] resolve failed\n");
        return;
    }
    console_puts(host);
    console_puts(" -> ");
    put_ip(ip);
    console_puts("\n");
}

/* ---- ntpsync ---- */

static void cmd_ntpsync(const char *args)
{
    struct rtc_time before;
    unsigned int unix_sec;
    int hms[3];
    int rc;
    unsigned int server;
    const char *p = args, *t;
    int l;

    if (!net_ready()) {
        console_puts("[err] network not present\n");
        return;
    }

    server = netcfg_ntp();
    l = next_tok(&p, &t);
    if (l > 0) {                        /* 显式指定服务器(域名或 IP) */
        char host[64];
        int i;

        if (l >= (int)sizeof(host)) {
            console_puts("[err] name too long\n");
            return;
        }
        for (i = 0; i < l; i++)
            host[i] = t[i];
        host[l] = '\0';
        if (net_resolve(host, &server) != 0) {
            console_puts("[err] cannot resolve server\n");
            return;
        }
    }

    console_puts("NTP sync -> ");
    put_ip(server);
    console_puts(":123 ...\n");

    if (rtc_read(&before) == 0) {
        console_puts("  RTC before : ");
        put_rtc_hms(&before);
        console_puts("\n");
    } else {
        console_puts("  RTC before : unreadable\n");
    }

    rc = ntp_sync(server, &unix_sec, hms);
    switch (rc) {
    case 0:
        console_puts("  [ok ] server reply accepted\n");
        console_puts("  UTC now    : ");
        put_dec(unix_sec);
        console_puts(" (unix seconds)\n");
        console_puts("  RTC set to : ");
        if (hms[0] < 10) console_putc('0');
        put_dec((unsigned int)hms[0]);
        console_putc(':');
        if (hms[1] < 10) console_putc('0');
        put_dec((unsigned int)hms[1]);
        console_putc(':');
        if (hms[2] < 10) console_putc('0');
        put_dec((unsigned int)hms[2]);
        console_puts("  (UTC+8, written to CMOS)\n");
        console_puts("  run 'date' to verify\n");
        break;
    case -1:
        console_puts("  [err] stack not ready\n");
        break;
    case -2:
        console_puts("  [err] ARP failed - no reply on route\n");
        break;
    case -3:
        console_puts("  [err] UDP send failed\n");
        break;
    case -4:
        console_puts("  [err] NTP timeout (5s) - check network\n");
        break;
    case -5:
        console_puts("  [err] NTP epoch before 1970 - bad reply\n");
        break;
    case -6:
        console_puts("  [err] time received but RTC write failed\n");
        break;
    default:
        console_puts("  [err] sync failed\n");
        break;
    }
}

/* ---- tcp:单连接阻塞 TCP 测试命令 ---- */

static void cmd_tcp(const char *args)
{
    const char *p = args, *t1 = 0, *t2 = 0, *t3 = 0;
    int l1, l2, l3;

    l1 = next_tok(&p, &t1);
    if (l1 < 0) {                       /* 无参:状态 */
        console_puts("tcp state: ");
        console_puts(tcp_state_name());
        if (tcp_local_port() != 0) {
            console_puts("  local port ");
            put_dec((unsigned int)tcp_local_port());
        }
        console_puts("\n");
        return;
    }
    if (!net_ready()) {
        console_puts("[err] network not present\n");
        return;
    }

    if (tok_eq(t1, l1, "conn")) {
        unsigned int ip = 0;
        unsigned short port = 0;
        char host[96];
        unsigned int i;
        int rc;

        l2 = next_tok(&p, &t2);
        l3 = next_tok(&p, &t3);
        if (l2 < 0 || l3 < 0 || l2 >= 96) {
            console_puts("usage: tcp conn <ip|host> <port>\n");
            return;
        }
        for (i = 0; i < (unsigned int)l2; i++)
            host[i] = t2[i];
        host[l2] = '\0';
        if (net_parse_ip(host, &ip) != 0 && net_resolve(host, &ip) != 0) {
            console_puts("  [err] cannot resolve host\n");
            return;
        }
        for (i = 0; i < (unsigned int)l3 && t3[i] >= '0' && t3[i] <= '9'; i++)
            port = (unsigned short)(port * 10u
                    + (unsigned int)(t3[i] - '0'));
        if (port == 0) {
            console_puts("  [err] bad port\n");
            return;
        }
        console_puts("  connecting...\n");
        rc = tcp_connect(ip, port);
        if (rc == 0) {
            console_puts("  [ok ] established\n");
        } else {
            console_puts("  [err] connect failed rc=");
            put_dec((unsigned int)(-rc));
            console_puts("\n");
        }
        return;
    }

    if (tok_eq(t1, l1, "listen")) {
        unsigned short port = 0;
        unsigned int i;
        int rc;

        l2 = next_tok(&p, &t2);
        if (l2 < 0) {
            console_puts("usage: tcp listen <port>\n");
            return;
        }
        for (i = 0; i < (unsigned int)l2 && t2[i] >= '0' && t2[i] <= '9'; i++)
            port = (unsigned short)(port * 10u
                    + (unsigned int)(t2[i] - '0'));
        if (port == 0) {
            console_puts("  [err] bad port\n");
            return;
        }
        console_puts("  waiting for connection...\n");
        rc = tcp_listen(port);
        if (rc == 0) {
            console_puts("  [ok ] established\n");
        } else {
            console_puts("  [err] listen failed rc=");
            put_dec((unsigned int)(-rc));
            console_puts("\n");
        }
        return;
    }

    if (tok_eq(t1, l1, "send")) {
        char buf[256];
        unsigned int n = 0;
        int rc;

        while (*p == ' ')
            p++;
        while (*p && n < sizeof buf - 2u)
            buf[n++] = *p++;
        if (n == 0) {
            console_puts("usage: tcp send <text>\n");
            return;
        }
        buf[n++] = '\n';
        rc = tcp_send((const unsigned char *)buf, n);
        if (rc >= 0) {
            console_puts("  [ok ] sent ");
            put_dec((unsigned int)rc);
            console_puts(" bytes\n");
        } else {
            console_puts("  [err] send failed\n");
        }
        return;
    }

    if (tok_eq(t1, l1, "recv")) {
        unsigned char buf[1536];
        int n = tcp_recv(buf, sizeof buf);
        int i;

        if (n < 0) {
            console_puts("  [err] recv failed\n");
            return;
        }
        if (n == 0) {
            console_puts("  [eof] peer closed\n");
            return;
        }
        for (i = 0; i < n; i++)
            console_putc((char)buf[i]);
        console_puts("\n  [ok ] got ");
        put_dec((unsigned int)n);
        console_puts(" bytes\n");
        return;
    }

    if (tok_eq(t1, l1, "close")) {
        tcp_close();
        console_puts("  [ok ] closed\n");
        return;
    }

    console_puts("usage: tcp [conn <ip|host> <port> | listen <port> |\n"
                 "           send <text> | recv | close]\n");
}

void cmd_net_init(void)
{
    static const struct shell_command cmds[] = {
        { "netinfo", "show NIC and network configuration",  cmd_netinfo },
        { "net",     "show config / net set <key> <a.b.c.d>", cmd_net },
        { "ping",    "ICMP echo (ip or hostname)",          cmd_ping },
        { "dns",     "resolve a hostname",                  cmd_dns },
        { "ntpsync", "sync time via NTP [server]",          cmd_ntpsync },
        { "tcp",     "single-connection TCP test",           cmd_tcp }
    };
    unsigned int i;

    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        shell_register_command(&cmds[i]);
}
