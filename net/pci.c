/* net/pci.c - PCI 总线枚举
 *
 * x86 PCI 通过 0xCF8/0xCFC 两级端口访问配置空间:
 *   地址寄存器 bit31=enable, bit23..16=bus, bit15..11=dev,
 *   bit10..8=fn, bit7..2=寄存器 dword 序号。
 * QEMU x86_64 -M pc 只有一个 PCI 域,总线 0..255 顺序扫即可。
 */
#include "pci.h"

unsigned int pci_cmd_dbg;   /* 诊断:使能后读回的 Command 寄存器(临时) */

static unsigned char found_bus, found_dev, found_fn;

unsigned int pci_config_read(unsigned char bus, unsigned char dev,
                             unsigned char fn, unsigned char reg)
{
    unsigned int addr = 0x80000000u
                      | ((unsigned int)bus  << 16)
                      | ((unsigned int)dev  << 11)
                      | ((unsigned int)fn   << 8)
                      | ((unsigned int)(reg & 0xFCu));

    pci_outl(PCI_CONFIG_ADDR, addr);
    return pci_inl(PCI_CONFIG_DATA);
}

void pci_config_write(unsigned char bus, unsigned char dev,
                      unsigned char fn, unsigned char reg,
                      unsigned int val)
{
    unsigned int addr = 0x80000000u
                      | ((unsigned int)bus  << 16)
                      | ((unsigned int)dev  << 11)
                      | ((unsigned int)fn   << 8)
                      | ((unsigned int)(reg & 0xFCu));

    pci_outl(PCI_CONFIG_ADDR, addr);
    pci_outl(PCI_CONFIG_DATA, val);
}

int pci_find_device(unsigned int vendor, unsigned int device,
                    unsigned int *bar0, unsigned char *irq)
{
    unsigned int bus, dev;
    unsigned char fn;

    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            for (fn = 0; fn < 8; fn++) {
                unsigned int id = pci_config_read((unsigned char)bus,
                                                  (unsigned char)dev,
                                                  fn, 0x00);
                if ((id & 0xFFFFu) != vendor)
                    continue;           /* vendor 不匹配,看下一个 */
                if ((id >> 16) != device)
                    continue;

                /* BAR0:配置偏移 0x10。只接受 32 位内存 BAR(低 4 位
                 * bit0=0 表示 memory BAR;取高位掩码) */
                {
                    unsigned int raw = pci_config_read((unsigned char)bus,
                                                       (unsigned char)dev,
                                                       fn, 0x10);
                    if ((raw & 1u) != 0)
                        continue;       /* IO BAR,不是我们要的 */
                    *bar0 = raw & 0xFFFFFFF0u;
                }
                /* 中断线(可选读,轮询模式下仅做展示) */
                {
                    unsigned int il = pci_config_read((unsigned char)bus,
                                                      (unsigned char)dev,
                                                      fn, 0x3C);
                    *irq = (unsigned char)(il & 0xFFu);
                }

                /* 命中设备:开 Memory Space + Bus Master。
                 * QEMU 8.2.x 新修订严格检查 PCI bus master —— 没有它
                 * e1000 不发起 DMA,收发全部瘫痪(2026-10 实测定位)。
                 * 注意:这里只是保险性预开;真正的使能在
                 * pci_enable_master(),必须放在 e1000 软复位之后
                 * (QEMU 对 CTRL.RST 会连带 pci_device_reset 清本位) */
                found_bus = (unsigned char)bus;
                found_dev = (unsigned char)dev;
                found_fn  = fn;
                return 0;
            }
        }
    }
    return -1;
}

/* 使能找到的网卡设备的 Memory Space + Bus Master。
 * 必须在 e1000 软复位之后调用(QEMU 对 CTRL.RST 会连带
 * pci_device_reset,把 command 寄存器打回默认值)。 */
void pci_enable_master(void)
{
    unsigned int cmd = pci_config_read(found_bus, found_dev, found_fn, 0x04)
                     & 0xFFFFu;

    cmd |= (1u << 1)      /* Memory Space Enable */
         | (1u << 2);     /* Bus Master Enable */
    pci_config_write(found_bus, found_dev, found_fn, 0x04, cmd);
    pci_cmd_dbg = pci_config_read(found_bus, found_dev, found_fn, 0x04)
                & 0xFFFFu;
}
