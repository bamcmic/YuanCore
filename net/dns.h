/* net/dns.h - 最小 DNS 解析器(A 记录)与主机名解析入口 */
#ifndef NET_DNS_H
#define NET_DNS_H

#include "net.h"

/* 主机名解析入口:
 *   点分十进制(10.0.2.2) -> 直通返回 0;
 *   否则按名字走 DNS(netcfg_dns(),默认 10.0.2.3),缓存最近一条。
 * 返回 0 成功(out_ip 主机序);负值=栈未就绪/解析失败/超时。 */
int net_resolve(const char *host, unsigned int *out_ip);

/* 仅 DNS 查询(点分十进制名字会失败)。dns 命令展示用 */
int dns_resolve(const char *name, unsigned int *out_ip);

#endif /* NET_DNS_H */
