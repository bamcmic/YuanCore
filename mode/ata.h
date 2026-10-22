/* mode/ata.h - ATA PIO 块设备(primary 通道, LBA28)
 *
 * 目标:给持久化文件系统提供 512 字节扇区读写。
 * 覆盖范围:
 *   - QEMU i440fx 默认 IDE(运行 `-drive if=ide`)
 *   - 真机 BIOS 模式下 SATA 控制器的 IDE 兼容模式
 *   - AHCI 原生模式/NVMe 不支持(后续单独驱动)
 *
 * 纯 PIO(端口 0x1F0-0x1F7),无中断无 DMA,轮询状态位 + 超时。
 * 只在启动阶段和文件系统同步时用,性能要求低。
 */
#ifndef MODE_ATA_H
#define MODE_ATA_H

#define ATA_SECTOR_SIZE 512u

/* 探测主盘。返回 0=就绪;负=无盘/识别失败(系统照常启动,只是不持久) */
int ata_init(void);

/* 是否就绪(有盘且识别成功) */
int ata_ready(void);

/* 读/写连续扇区。返回 0 成功,负=错误或未就绪 */
int ata_read_sectors(unsigned int lba, unsigned char *buf,
                     unsigned int count);
int ata_write_sectors(unsigned int lba, const unsigned char *buf,
                      unsigned int count);

/* LBA28 可寻址扇区总数(0=未知) */
unsigned int ata_sector_count(void);

#endif /* MODE_ATA_H */
