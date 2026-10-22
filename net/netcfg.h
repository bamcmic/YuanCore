/* net/netcfg.h - 网络配置层(IP/掩码/网关/DNS/NTP,可运行时改 + 持久化)
 *
 * 取代 net.h 时代的硬编码宏(QEMU user-net 默认值仍作为出厂默认)。
 * 配置保存为 /net.cfg(每行 key=点分十进制,与 /settings.cfg 同风格):
 *   ip=10.0.2.15
 *   mask=255.255.255.0
 *   gw=10.0.2.2
 *   dns=10.0.2.3
 *   ntp=203.107.6.102
 * net_init() 时加载;`net set` 改完即存。ramfs 掉电即失的老规矩不变。
 */
#ifndef NET_NETCFG_H
#define NET_NETCFG_H

#define NETCFG_PATH "/net.cfg"

/* 出厂默认(QEMU user network 标准网段) */
void netcfg_defaults(void);

/* 读 /net.cfg;文件不存在或损坏则保持当前值。返回 1=加载了文件 0=没有 */
int  netcfg_load(void);

/* 写 /net.cfg。返回 0 成功,负值=fs 错误 */
int  netcfg_save(void);

/* 当前值(主机序) */
unsigned int netcfg_ip(void);
unsigned int netcfg_netmask(void);
unsigned int netcfg_gateway(void);
unsigned int netcfg_dns(void);
unsigned int netcfg_ntp(void);

/* 修改一项(点分十进制字符串)。解析失败返回 -1;成功后自动落盘 */
int netcfg_set_ip(const char *dotted);
int netcfg_set_mask(const char *dotted);
int netcfg_set_gw(const char *dotted);
int netcfg_set_dns(const char *dotted);
int netcfg_set_ntp(const char *dotted);

/* 点分十进制 <-> 32 位主机序。"10.0.2.15" <-> 0x0A00020F */
int  net_parse_ip(const char *s, unsigned int *out);
void net_format_ip(unsigned int ip, char out[16]);   /* out 至少 16 字节 */

#endif /* NET_NETCFG_H */
