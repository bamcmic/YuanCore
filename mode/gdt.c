/* gdt.c - 全局描述符表
 *
 * GRUB 留下来的 GDT 不归我们管，接管后自己建一套：
 *   0x00 null
 *   0x08 内核代码 ring0
 *   0x10 内核数据 ring0
 *   0x18 用户代码 ring3（为以后多任务预留）
 *   0x20 用户数据 ring3
 * 平坦模型：基址 0，上限 4GB，4KB 粒度 + 32 位。
 */
#include "gdt.h"

struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char  base_mid;
    unsigned char  access;
    unsigned char  granularity;
    unsigned char  base_high;
} __attribute__((packed));

struct gdt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gp;

static void gdt_set_gate(int n, unsigned int base, unsigned int limit,
                         unsigned char access, unsigned char gran)
{
    gdt[n].base_low    = (unsigned short)(base & 0xFFFF);
    gdt[n].base_mid    = (unsigned char)((base >> 16) & 0xFF);
    gdt[n].base_high   = (unsigned char)((base >> 24) & 0xFF);
    gdt[n].limit_low   = (unsigned short)(limit & 0xFFFF);
    gdt[n].granularity = (unsigned char)((limit >> 16) & 0x0F);
    gdt[n].granularity |= (unsigned char)(gran & 0xF0);
    gdt[n].access      = access;
}

/* 段寄存器必须重新加载，否则还在用 GRUB 的旧选择子 */
static void gdt_flush(struct gdt_ptr *p)
{
    __asm__ volatile ("lgdt (%0)" : : "r"(p));

    __asm__ volatile (
        "ljmp $0x08, $1f\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        :
        :
        : "%eax"
    );
}

void gdt_init(void)
{
    unsigned int i;

    gp.limit = (unsigned short)(sizeof(gdt) - 1);
    gp.base  = (unsigned int)(unsigned long)gdt;

    gdt_set_gate(0, 0, 0, 0x00, 0x00);                  /* null */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);         /* 内核代码 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);         /* 内核数据 */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);         /* 用户代码 */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);         /* 用户数据 */

    /* TSS 槽位(0x28)留给 tss_init() 填 */
    for (i = 5; i < 6; i++)
        gdt_set_gate(i, 0, 0, 0x00, 0x00);

    gdt_flush(&gp);
}

/* TSS 描述符：access 0x89 = present + TSS(32bit available) */
void gdt_install_tss(unsigned int base, unsigned short limit)
{
    gdt_set_gate(5, base, limit, 0x89, 0x00);
    gdt_flush(&gp);
}
