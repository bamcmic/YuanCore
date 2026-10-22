/* x64/gdt64.c - x86-64 GDT（含 ring3 段与 TSS64）
 *
 * 长模式段布局:
 *   0x00 null
 *   0x08 kernel code (L=1)
 *   0x10 kernel data
 *   0x18 user code ring3 (L=1)      <- ring3 迁移新增
 *   0x20 user data ring3            <- ring3 迁移新增
 *   0x28 TSS64(16 字节描述符,占两个 8 字节槽位)
 *
 * TSS 在长模式只有三件事有用:rsp0/ist/iopb —— 我们只用 rsp0:
 * ring3 -> ring0(int 0x80 / IRQ) 时 CPU 从 TSS.rsp0 取内核栈。
 * 无任务切换语义,ltr 指一下即可。
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

/* TSS 描述符是 16 字节系统段,跨 GDT[5] 与 GDT[6] 两个槽位 */
struct tss_desc64 {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;            /* 0x89 = present | TSS64(available) */
    uint8_t  granularity;
    uint8_t  base_hi;
    uint32_t base_top;
    uint32_t zero;
} __attribute__((packed));

/* 64 位模式 lgdt 读 m16:64:limit 在 +0,base 必须紧跟在 +2(不能有 pad) */
struct gdt_ptr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* TSS64:长模式硬件只消费 rsp0/ist/iopb,其余历史包袱全置 0 */
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t iopb;              /* 指向 I/O 位图,无位图时 = sizeof(tss64) */
} __attribute__((packed));

static struct gdt_entry64 gdt[7];   /* 5 个 8 字节段 + TSS 占 5,6 两槽 */
static struct gdt_ptr64   gp;
static struct tss64       tss __attribute__((aligned(8)));

/* 中断备用栈(rsp0 指向其顶部),底部埋金丝雀 */
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
    gdt64_set(1, 0x9A, 0x20);   /* 64-bit kernel code: P|S|EXE, L=1 */
    gdt64_set(2, 0x92, 0x00);   /* 64-bit kernel data: P|S|W */
    gdt64_set(3, 0xFA, 0x20);   /* 64-bit user code ring3: P|DPL3|S|EXE, L=1 */
    gdt64_set(4, 0xF2, 0x00);   /* 64-bit user data ring3: P|DPL3|S|W */

    gp.limit = (uint16_t)(sizeof(gdt) - 1);
    gp.base  = (uint64_t)(unsigned long)gdt;

    gdt64_flush();
}

void tss64_init(void)
{
    struct tss_desc64 *desc = (struct tss_desc64 *)&gdt[5];
    uint64_t base = (uint64_t)(unsigned long)&tss;
    uint64_t top  = (uint64_t)(unsigned long)(irq_stack64
                    + sizeof(irq_stack64));

    /* 静态存储默认全零,只填硬件消费的字段 */
    tss.rsp0 = top & ~(uint64_t)0xFu;   /* 16 字节对齐的栈顶 */
    tss.iopb = (uint16_t)sizeof(tss);

    desc->limit_lo     = (uint16_t)(sizeof(tss) - 1);
    desc->base_lo      = (uint16_t)(base & 0xFFFFu);
    desc->base_mid     = (uint8_t)((base >> 16) & 0xFFu);
    desc->access       = 0x89;          /* present | TSS64 available */
    desc->granularity  = 0x00;
    desc->base_hi      = (uint8_t)((base >> 24) & 0xFFu);
    desc->base_top     = (uint32_t)(base >> 32);
    desc->zero         = 0;

    __asm__ volatile ("ltr %0" : : "rm"((uint16_t)0x28) : "memory");
}
