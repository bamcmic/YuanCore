/* tss.c - 任务状态段
 *
 * 只为一个目的：ring3 发生中断/异常时 CPU 能切到内核栈。
 * esp0 指向一段 16KB 的内核栈；不做硬件任务切换。
 */
#include "tss.h"
#include "gdt.h"
#include "io.h"

struct tss_struct {
    unsigned int prev;
    unsigned int esp0;
    unsigned int ss0;
    unsigned int esp1;
    unsigned int ss1;
    unsigned int esp2;
    unsigned int ss2;
    unsigned int cr3;
    unsigned int eip;
    unsigned int eflags;
    unsigned int eax, ecx, edx, ebx;
    unsigned int esp, ebp, esi, edi;
    unsigned int es, cs, ss, ds, fs, gs;
    unsigned int ldt;
    unsigned short trap, iomap_base;
} __attribute__((packed));

static struct tss_struct tss;
static unsigned char irq_stack[32768] __attribute__((aligned(16)));

/* 栈底金丝雀：中断栈溢出(向下越过最低地址)必然先踩掉它。
 * 内核主循环周期性检查，踩掉就大声报警，而不是默默腐蚀 TSS/邻居。 */
#define STACK_CANARY 0xC0DEFEEDu
static volatile unsigned int canary;

int tss_canary_ok(void)
{
    return *(volatile unsigned int *)irq_stack == STACK_CANARY;
}

void tss_init(void)
{
    unsigned int i;
    unsigned short sel = 0x28;

    for (i = 0; i < sizeof(tss) / 4; i++)
        ((unsigned int *)&tss)[i] = 0;

    tss.ss0  = 0x10;                                        /* 内核数据段 */
    tss.esp0 = (unsigned int)irq_stack + sizeof(irq_stack); /* 栈顶(向下长) */

    *(volatile unsigned int *)irq_stack = STACK_CANARY;

    gdt_install_tss((unsigned int)&tss, (unsigned short)(sizeof(tss) - 1));
    __asm__ volatile ("ltr %0" : : "m"(sel));
    canary = STACK_CANARY;
    (void)canary;
}
