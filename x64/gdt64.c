/* x64/gdt64.c - x86-64 GDT
 *
 * 长模式的段描述符只剩 code/data 两类,base/limit 语义基本作废,
 * 只需 null + 内核 code + 内核 data。
 * 无 ring3(TSS)之前不需要 TSS 描述符;
 * 中断栈金丝雀沿用 i386 版的巡检思路。
 */
#include "gdt64.h"

#include <stdint.h>

struct gdt_entry64 {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;       /* 含 L 位 */
    uint8_t  base_hi;
} __attribute__((packed));

/* 64 位模式 lgdt 读 m16:64:limit 在 +0,base 必须紧跟在 +2(不能有 pad) */
struct gdt_ptr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry64 gdt[3];
static struct gdt_ptr64   gp;

/* 中断备用栈(未来 ring3/嵌套中断用),底部埋金丝雀 */
static unsigned char irq_stack64[32768] __attribute__((aligned(16)));
#define CANARY 0xC0DEFEEDC0DEFEEDULL

int tss_canary_ok(void)
{
    uint64_t *p = (uint64_t *)irq_stack64;

    return p[0] == CANARY;
}

static void gdt64_set(int n, uint8_t access, uint8_t gran)
{
    gdt[n].limit_lo     = 0xFFFF;
    gdt[n].base_lo      = 0;
    gdt[n].base_mid     = 0;
    gdt[n].access       = access;
    gdt[n].granularity  = gran;
    gdt[n].base_hi      = 0;
}

static inline void gdt64_flush(void)
{
    /* 载入新 GDT;CS 已由 boot32.asm 的 ljmp 设为 0x08 且新旧布局
     * 一致,无需重装 CS(64 位模式下 ljmp 立即数形式不可用)。 */
    __asm__ volatile (
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        :
        : "m"(gp)
        : "ax", "memory");
}

void gdt64_init(void)
{
    uint64_t *canary = (uint64_t *)irq_stack64;

    canary[0] = CANARY;         /* 栈底金丝雀 */

    gdt64_set(0, 0x00, 0x00);   /* null */
    gdt64_set(1, 0x9A, 0x20);   /* 64-bit code: P|S|EXE, L=1 */
    gdt64_set(2, 0x92, 0x00);   /* 64-bit data: P|S|W */

    gp.limit = (uint16_t)(sizeof(gdt) - 1);
    gp.base  = (uint64_t)(unsigned long)gdt;

    gdt64_flush();
}
