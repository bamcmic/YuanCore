/* net/e1000.h - Intel 82540EM (QEMU e1000) 网卡驱动接口 */
#ifndef NET_E1000_H
#define NET_E1000_H

#include "net.h"

/* 复位并初始化网卡。mmio 为 BAR0 物理基址(恒等映射下直接可访问)。
 * 返回 0 成功。 */
int e1000_init(unsigned int mmio);

/* 读 6 字节 MAC 地址 */
void e1000_get_mac(unsigned char mac[6]);

/* 发一帧(不含 FCS)。阻塞等待发送完成,返回 0/-1 */
int e1000_send(const unsigned char *buf, unsigned int len);

/* 收一帧(轮询)。有包则拷入 buf 并返回包长;无包返回 0 */
unsigned int e1000_recv(unsigned char *buf, unsigned int max);

/* 链路状态:0 = up,-1 = 等待超时 */
int e1000_link_up(void);

#endif /* NET_E1000_H */
