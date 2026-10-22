/* mode/ata.c - ATA PIO 块设备(primary 通道, LBA28)
 *
 * 寄存器(primary): 0x1F0 data, 0x1F1 error/features, 0x1F2 sectors,
 *                  0x1F3..0x1F5 LBA low/mid/high, 0x1F6 drive/head,
 *                  0x1F7 status/command, 0x3F6 control
 *
 * 状态位: 0x80 BSY, 0x08 DRQ, 0x20 DF, 0x01 ERR
 */
#include "ata.h"
#include "io.h"

#define ATA_IO      0x1F0
#define ATA_CTRL    0x3F6

#define ST_ERR      0x01
#define ST_DRQ      0x08
#define ST_DF       0x20
#define ST_BSY      0x80

#define CMD_READ    0x20
#define CMD_WRITE   0x30
#define CMD_FLUSH   0xE7
#define CMD_IDENT   0xEC

/* 轮询上限:每次端口访问 ~1us,20 万次约 0.2s,足够慢盘 */
#define ATA_SPIN    200000u

static int          ata_up;
static unsigned int ata_sectors;

static unsigned char ata_status(void)
{
    return inb(ATA_IO + 7);
}

/* 数据端口 0x1F0 是 16 位寄存器:必须 outw/inw。
 * 用两次 outb 会把高字节写进 0x1F1(error/features),数据错位; */
static inline void ata_outw(unsigned short port, unsigned short v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}

/* 等 BSY 清。返回 0 成功,-1 超时/错误 */
static int ata_wait_idle(void)
{
    unsigned int i;

    for (i = 0; i < ATA_SPIN; i++) {
        unsigned char st = ata_status();

        if ((st & ST_BSY) == 0)
            return (st & ST_ERR) ? -1 : 0;
    }
    return -1;
}

/* 等 DRQ 置(数据可读/可写)。返回 0 成功,-1 超时/错误 */
static int ata_wait_drq(void)
{
    unsigned int i;

    for (i = 0; i < ATA_SPIN; i++) {
        unsigned char st = ata_status();

        if (st & ST_ERR)
            return -1;
        if ((st & ST_BSY) == 0 && (st & ST_DRQ))
            return 0;
    }
    return -1;
}

/* 400ns 等待(读 alt status 4 次) */
static void ata_delay400(void)
{
    inb(ATA_CTRL);
    inb(ATA_CTRL);
    inb(ATA_CTRL);
    inb(ATA_CTRL);
}

int ata_init(void)
{
    unsigned short id[256];
    unsigned int i;

    ata_up = 0;
    ata_sectors = 0;

    /* 选主盘,发 IDENTIFY */
    outb(ATA_IO + 6, 0xA0);
    ata_delay400();
    outb(ATA_IO + 2, 0);
    outb(ATA_IO + 3, 0);
    outb(ATA_IO + 4, 0);
    outb(ATA_IO + 5, 0);
    outb(ATA_IO + 7, CMD_IDENT);

    ata_delay400();
    if (ata_status() == 0)
        return -1;                        /* 无设备 */

    if (ata_wait_idle() != 0) {
        if (ata_status() & ST_ERR)
            inb(ATA_IO + 1);              /* 清 error 寄存器 */
        return -1;
    }
    if (ata_wait_drq() != 0)
        return -1;

    for (i = 0; i < 256; i++)
        id[i] = inw(ATA_IO);

    /* words 60..61 = LBA28 总扇区数(小端 32 位) */
    ata_sectors = (unsigned int)id[60]
                | ((unsigned int)id[61] << 16);
    if (ata_sectors == 0)
        return -1;

    ata_up = 1;
    return 0;
}

int ata_ready(void)
{
    return ata_up;
}

unsigned int ata_sector_count(void)
{
    return ata_sectors;
}

/* 设置读/写起始位置(LBA28) */
static void ata_set_lba(unsigned int lba, unsigned int count)
{
    outb(ATA_IO + 6, (unsigned char)((0xE0u | ((lba >> 24) & 0x0Fu))));
    outb(ATA_IO + 1, 0);
    outb(ATA_IO + 2, (unsigned char)(count & 0xFFu));
    outb(ATA_IO + 3, (unsigned char)(lba & 0xFFu));
    outb(ATA_IO + 4, (unsigned char)((lba >> 8) & 0xFFu));
    outb(ATA_IO + 5, (unsigned char)((lba >> 16) & 0xFFu));
}

int ata_read_sectors(unsigned int lba, unsigned char *buf,
                     unsigned int count)
{
    unsigned int s, w;

    if (!ata_up || count == 0 || lba + count > ata_sectors)
        return -1;

    ata_set_lba(lba, count);
    outb(ATA_IO + 7, CMD_READ);

    for (s = 0; s < count; s++) {
        if (ata_wait_drq() != 0)
            return -2;
        for (w = 0; w < 256; w++) {
            unsigned short v = inw(ATA_IO);

            buf[w * 2]     = (unsigned char)(v & 0xFFu);
            buf[w * 2 + 1] = (unsigned char)(v >> 8);
        }
        buf += ATA_SECTOR_SIZE;
        ata_delay400();
    }
    return 0;
}

int ata_write_sectors(unsigned int lba, const unsigned char *buf,
                      unsigned int count)
{
    unsigned int s, w;

    if (!ata_up || count == 0 || lba + count > ata_sectors)
        return -1;

    ata_set_lba(lba, count);
    outb(ATA_IO + 7, CMD_WRITE);

    for (s = 0; s < count; s++) {
        if (ata_wait_drq() != 0)
            return -2;
        for (w = 0; w < 256; w++) {
            ata_outw(ATA_IO,
                     (unsigned short)((unsigned short)buf[w * 2]
                     | ((unsigned short)buf[w * 2 + 1] << 8)));
        }
        buf += ATA_SECTOR_SIZE;
        ata_delay400();
    }

    /* 刷缓存,确保掉电前数据落盘 */
    outb(ATA_IO + 7, CMD_FLUSH);
    ata_wait_idle();
    return 0;
}
