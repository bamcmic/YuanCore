/* net/icmp.h - ICMP echo(echo request/reply),ping 命令的后端 */
#ifndef NET_ICMP_H
#define NET_ICMP_H

#include "net.h"

/* 发一个 echo request 并阻塞等 reply。
 * 返回 0 收到匹配 reply(out_rtt 写入 tick 差,100Hz);
 * -1 栈未就绪 / 发送失败;-2 超时未收到。 */
int icmp_ping(unsigned int ip, unsigned int timeout_ticks,
              unsigned int *out_rtt);

/* net.c 收包分发入口:packet = ICMP 报文(IP payload,含 8 字节头) */
void icmp_handle(const unsigned char *pkt, unsigned int len,
                 unsigned int src_ip);

#endif /* NET_ICMP_H */
