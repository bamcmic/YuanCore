/* net/pci.h - PCI 总线枚举 */
#ifndef NET_PCI_H
#define NET_PCI_H

/* PCI 配置空间端口(标准 x86) */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* e1000 (Intel 82540EM) 的 PCI 标识,QEMU -device e1000 默认即此 */
#define PCI_VENDOR_INTEL   0x8086u
#define PCI_DEVICE_E1000   0x100Eu

/* 32 位端口读写(PCI 配置空间用;io.h 只有 8/16 位,这里自含一套) */
static inline void pci_outl(unsigned short port, unsigned int val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned int pci_inl(unsigned short port)
{
    unsigned int ret;

    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 按 (bus,dev,fn) 读配置空间第 reg 个 32 位字(reg 按字节偏移且 4 对齐) */
unsigned int pci_config_read(unsigned char bus, unsigned char dev,
                             unsigned char fn, unsigned char reg);

/* 写配置空间 32 位字(Command 寄存器使能用) */
void pci_config_write(unsigned char bus, unsigned char dev,
                      unsigned char fn, unsigned char reg,
                      unsigned int val);

/* 扫描全部总线找 vendor/device 匹配的第一个设备。
 * 找到填 bar0(内存基址,只认 32 位内存 BAR)与 irq(中断线,备用),
 * 返回 0;找不到返回 -1。 */
int pci_find_device(unsigned int vendor, unsigned int device,
                    unsigned int *bar0, unsigned char *irq);

/* 诊断:pci_find_device 使能后读回的 Command 寄存器值(临时) */
extern unsigned int pci_cmd_dbg;

/* 使能已找到网卡的 Memory Space + Bus Master。
 * 必须在 e1000_init(软复位)之后调用 */
void pci_enable_master(void);

#endif /* NET_PCI_H */
