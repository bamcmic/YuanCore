/* idt.c - 中断描述符表与异常分发 */
#include "idt.h"
#include "io.h"
#include "usermode.h"

void print_panic(const struct registers *r);   /* kernel.c */

struct idt_entry {
    unsigned short base_lo;
    unsigned short sel;          /* 代码段选择子，内核代码 = 0x08 */
    unsigned char  always0;
    unsigned char  flags;        /* P|DPL|0|1110 = 0x8E */
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

static isr_handler_t isr_handlers[256];

/* isr.asm 提供的桩地址表：0-31 异常，32-47 IRQ */
extern void *isr_stub_table[48];
extern void *int80_stub;

void idt_set_gate(int n, unsigned int handler)
{
    idt[n].base_lo = (unsigned short)(handler & 0xFFFF);
    idt[n].base_hi = (unsigned short)((handler >> 16) & 0xFFFF);
    idt[n].sel     = 0x08;
    idt[n].always0 = 0;
    idt[n].flags   = 0x8E;       /* present, ring0, 32-bit interrupt gate */
}

/* ring3 可触发的门（系统调用） */
void idt_set_gate_user(int n, unsigned int handler)
{
    idt_set_gate(n, handler);
    idt[n].flags = 0xEE;         /* present, DPL=3, 32-bit interrupt gate */
}

void isr_register(int no, isr_handler_t h)
{
    if (no >= 0 && no < 256)
        isr_handlers[no] = h;
}

__attribute__((noreturn))
static void panic(const struct registers *r)
{
    cli();
    /* 异常发生时控制台未必可用，这里直接走 VGA 文本，最保险 */
    extern void print_panic(const struct registers *r);
    print_panic(r);
    for (;;)
        hlt();
}

/* isr.asm 调用：CPU 异常入口（以及 int 0x80） */
void isr_dispatch(struct registers *r)
{
    if (r->int_no < 32) {
        if (isr_handlers[r->int_no] != 0) {
            isr_handlers[r->int_no](r);
            return;
        }
        /* 用户态异常：杀掉程序回到 shell，而不是把整个系统带崩 */
        if ((r->cs & 3) != 0) {
            cli();
            print_panic(r);
            user_exit();
        }
        panic(r);
        return;
    }

    if (r->int_no >= 32 && isr_handlers[r->int_no] != 0)
        isr_handlers[r->int_no](r);
}

void idt_init(void)
{
    int i;

    for (i = 0; i < 48; i++)
        idt_set_gate(i, (unsigned int)(unsigned long)isr_stub_table[i]);

    /* 系统调用门：ring3 可触发 */
    idt_set_gate_user(0x80, (unsigned int)(unsigned long)int80_stub);

    idtp.limit = (unsigned short)(sizeof(idt) - 1);
    idtp.base  = (unsigned int)(unsigned long)idt;
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}
