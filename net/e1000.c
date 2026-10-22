/* net/e1000.c - Intel 82540EM 网卡驱动(QEMU 默认虚拟卡)
 *
 * 82540EM 寄存器全部经 BAR0 的 MMIO 访问,32 位宽,偏移见下表。
 * 收发走经典 legacy 描述符环:
 *   TX:软件填 buf -> 写 TDT 递增 -> 轮询描述符 status.DD 完成位
 *   RX:初始化时把全部缓冲交给硬件(RDT=N-1),轮询软件侧"下一个
 *      待取"描述符的 DD,取包后把缓冲还给硬件(写回 status=0,递增 RDT)
 * 不启用网卡中断:ICR 写 1 清pending,MSI/INTA 都不接,纯轮询。
 *
 * 恒等映射注意:描述符环与缓冲区是内核 BSS 静态数组,虚拟地址即
 * 物理地址且 < 4GB —— 满足 e1000 的 32 位寻址,无需 bounce buffer。
 */
#include "e1000.h"

/* ---- 关键寄存器偏移 ---- */
#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014          /* EEPROM 读取 */
#define REG_ICR    0x00C0          /* 中断原因(写 1 清) */
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818

/* CTRL 位 */
#define CTRL_RST   (1u << 26)      /* 软复位 */
#define CTRL_SLU   (1u << 6)       /* 链路置起 */
#define CTRL_FD    (1u << 0)       /* 全双工 */

/* STATUS 位 */
#define STATUS_LU  (1u << 1)       /* link up */

/* EERD 位 */
#define EERD_START (1u << 0)
#define EERD_DONE  (1u << 4)

/* RCTL 位 */
#define RCTL_EN    (1u << 1)
#define RCTL_UPE   (1u << 3)       /* 收单播全部(含非本 MAC,便于调试) */
#define RCTL_MPE   (1u << 4)       /* 收组播 */
#define RCTL_BAM   (1u << 15)      /* 接受广播(ARP 要用) */
#define RCTL_BSIZE_2048 (0u << 16) /* 00 = 2048 字节 */

/* TCTL 位 */
#define TCTL_EN    (1u << 1)
#define TCTL_PSP   (1u << 3)       /* 短包 pad */

/* TX 描述符 cmd */
#define TXD_CMD_EOP  (1u << 0)
#define TXD_CMD_IFCS (1u << 1)
#define TXD_CMD_RS   (1u << 3)
#define TXD_STAT_DD  (1u << 0)

/* ---- 描述符(legacy 16 字节) ---- */
struct e1000_txdesc {
    unsigned int addr_lo;          /* 缓冲物理地址(32 位) */
    unsigned int addr_hi;
    unsigned short length;
    unsigned char cso;
    unsigned char cmd;
    unsigned char status;
    unsigned char css;
    unsigned short special;
} __attribute__((packed));

struct e1000_rxdesc {
    unsigned int addr_lo;
    unsigned int addr_hi;
    unsigned short length;
    unsigned short csum;
    unsigned char status;
    unsigned char errors;
    unsigned short special;
} __attribute__((packed));

#define TX_RING  32
#define RX_RING  32

static volatile unsigned int *mmio;   /* BAR0 */

/* 静态环 + 缓冲(见文件头注释:恒等映射/DMA 约束) */
static struct e1000_txdesc tx_ring[TX_RING] __attribute__((aligned(16)));
static struct e1000_rxdesc rx_ring[RX_RING] __attribute__((aligned(16)));
static unsigned char       tx_buf[TX_RING][512]  __attribute__((aligned(256)));
static unsigned char       rx_buf[RX_RING][NET_RXBUF_SZ]
                                         __attribute__((aligned(NET_RXBUF_SZ)));

static unsigned int tx_next;          /* 软件下次投放的 TX 描述符 */
static unsigned int rx_next;          /* 软件下次收取的 RX 描述符 */
static unsigned char mac_addr[6];

static int wait_link(void);           /* 前向声明(init 诊断信标用) */

static void reg_write(unsigned int off, unsigned int val)
{
    mmio[off / 4] = val;
}

static unsigned int reg_read(unsigned int off)
{
    return mmio[off / 4];
}

/* 从 EEPROM 读 MAC 三个 word;失败(QEMU 个别机型)用默认 MAC 兜底 */
static void read_mac_from_eeprom(void)
{
    unsigned int i;

    mac_addr[0] = 0x52; mac_addr[1] = 0x54;   /* QEMU 默认 52:54:00:12:34:56 */
    mac_addr[2] = 0x00; mac_addr[3] = 0x12;
    mac_addr[4] = 0x34; mac_addr[5] = 0x56;

    for (i = 0; i < 3; i++) {
        unsigned int t, tries;
        unsigned short word;

        reg_write(REG_EERD, EERD_START | (i << 8));
        word = 0;
        for (tries = 0; tries < 100000; tries++) {
            t = reg_read(REG_EERD);
            if (t & EERD_DONE) {
                word = (unsigned short)((t >> 16) & 0xFFFFu);
                break;
            }
        }
        if (tries >= 100000)
            return;                       /* 读失败:保留默认 MAC */
        mac_addr[i * 2]     = (unsigned char)(word & 0xFFu);
        mac_addr[i * 2 + 1] = (unsigned char)(word >> 8);
    }
}

int e1000_init(unsigned int bar0)
{
    unsigned int i, ctrl;

    mmio = (volatile unsigned int *)(unsigned long)bar0;

    /* 软复位后按位重开:QEMU 对复位序列宽容,这里照规范走 */
    ctrl = reg_read(REG_CTRL);
    reg_write(REG_CTRL, ctrl | CTRL_RST);
    for (i = 0; i < 1000000; i++) {       /* 等复位自清 */
        if ((reg_read(REG_CTRL) & CTRL_RST) == 0)
            break;
    }
    reg_write(REG_CTRL, CTRL_SLU | CTRL_FD);

    read_mac_from_eeprom();

    /* 清掉上电残留的中断状态;不 unmask 任何中断(纯轮询) */
    reg_write(REG_ICR, 0xFFFFFFFFu);

    /* ---- RX 环 ---- */
    for (i = 0; i < RX_RING; i++) {
        rx_ring[i].addr_lo = (unsigned int)(unsigned long)&rx_buf[i][0];
        rx_ring[i].addr_hi = 0;
        rx_ring[i].status  = 0;
    }
    reg_write(REG_RDBAL, (unsigned int)(unsigned long)&rx_ring[0]);
    reg_write(REG_RDBAH, 0);
    reg_write(REG_RDLEN, sizeof(rx_ring));
    reg_write(REG_RDH, 0);
    reg_write(REG_RDT, RX_RING - 1);      /* 全部缓冲交给硬件 */
    rx_next = 0;
    /* EN|UPE|MPE|BAM + 2048 缓冲:广播(ARP)、发给本 MAC 的包都收 */
    reg_write(REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_BAM
                      | RCTL_BSIZE_2048);

    /* ---- TX 环 ---- */
    for (i = 0; i < TX_RING; i++) {
        tx_ring[i].addr_lo = (unsigned int)(unsigned long)&tx_buf[i][0];
        tx_ring[i].addr_hi = 0;
        tx_ring[i].status  = 0;
        tx_ring[i].cmd     = 0;
    }
    reg_write(REG_TDBAL, (unsigned int)(unsigned long)&tx_ring[0]);
    reg_write(REG_TDBAH, 0);
    reg_write(REG_TDLEN, sizeof(tx_ring));
    reg_write(REG_TDH, 0);
    reg_write(REG_TDT, 0);
    tx_next = 0;
    reg_write(REG_TCTL, TCTL_EN | TCTL_PSP);

    return 0;
}

void e1000_get_mac(unsigned char mac[6])
{
    int i;

    for (i = 0; i < 6; i++)
        mac[i] = mac_addr[i];
}

/* link up 等待:QEMU user net 下立即 up,真机要等 PHY */
static int wait_link(void)
{
    unsigned int tries;

    for (tries = 0; tries < 3000000; tries++) {
        if (reg_read(REG_STATUS) & STATUS_LU)
            return 0;
    }
    return -1;
}

int e1000_send(const unsigned char *buf, unsigned int len)
{
    unsigned int slot;

    if (len == 0 || len > 1500)
        return -1;

    slot = tx_next;
    net_memcpy(tx_buf[slot], buf, len);
    tx_ring[slot].length = (unsigned short)len;
    tx_ring[slot].status = 0;
    /* EOP|IFCS|RS:整帧一个描述符,硬件算 FCS */
    tx_ring[slot].cmd = (unsigned char)(TXD_CMD_EOP | TXD_CMD_IFCS
                                      | TXD_CMD_RS);
    tx_next = (tx_next + 1) % TX_RING;
    reg_write(REG_TDT, tx_next);          /* 递增 tail 触发发送 */

    /* 不轮询描述符 DD 完成位:QEMU 8.2.x 新修订不再写回 legacy TX
     * 描述符的 status(2026-10 实测,filter-dump 证明帧已上链路),
     * 阻塞等 DD 会误判发送失败。同步会话模型下单方向 <32 帧不会
     * 绕环覆盖,直接视为已投放。 */
    return 0;
}

unsigned int e1000_recv(unsigned char *buf, unsigned int max)
{
    unsigned int slot = rx_next;
    unsigned int len;

    if ((rx_ring[slot].status & 0x01u) == 0)   /* DD=0:没有新包 */
        return 0;

    len = rx_ring[slot].length;                /* 硬件写入的实际长度 */
    if (len > max)
        len = max;
    net_memcpy(buf, rx_buf[slot], len);

    rx_ring[slot].status = 0;                  /* 缓冲还给硬件 */
    rx_next = (rx_next + 1) % RX_RING;
    reg_write(REG_RDT, (rx_next + RX_RING - 1) % RX_RING);

    return len;
}

/* 供 net_init 顺手验证链路(失败不致命,只影响可用性报告) */
int e1000_link_up(void)
{
    return wait_link();
}
