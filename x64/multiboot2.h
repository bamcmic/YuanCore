/* x64/multiboot2.h - Multiboot2 引导信息解析(x86-64 版)
 *
 * GRUB 仍作为引导器(其 EFI 版与 BIOS 版都支持 Multiboot2),
 * 32 位入口桩(x64/boot32.asm)拿到 MBI 物理地址后由 kernel64.c
 * 解析 tag 列表,并合成本项目既有的 multiboot1 风格信息结构,
 * 使 fb/pmm/desktop 等模块零改动复用。
 */
#ifndef X64_MULTIBOOT2_H
#define X64_MULTIBOOT2_H

#include <stdint.h>

/* 魔数 */
#define MB2_HEADER_MAGIC   0xE85250D6u
#define MB2_BOOT_MAGIC     0x36D76289u

/* 信息 tag 类型(本内核用到的;编号遵循 Multiboot2 规范 3.3 节) */
#define MB2_TAG_END        0u
#define MB2_TAG_MEMINFO    4u      /* mem_lower/mem_upper */
#define MB2_TAG_MMAP       6u      /* memory map */
#define MB2_TAG_FRAMEBUFFER 8u

/* multiboot2 mmap 条目布局(base/length/type) */
struct mb2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t _pad;
};

/* multiboot1 mmap 条目(合成目标格式,pmm.c 按它遍历):
 *   uint32 size; uint64 base; uint64 length; uint32 type;  */
#define MB1_MMAP_ENTRY_SIZE 24u

#define MB1_MMAP_TYPE_AVAILABLE 1u

#endif /* X64_MULTIBOOT2_H */
