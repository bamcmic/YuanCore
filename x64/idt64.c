/* x64/idt64.c - x86-64 中断描述符表与分发
 *
 * 门描述符 16 字节:offset_lo16 sel16 ist8 type8 offset_mid16 offset_hi32 zero32
 * 异常分发:ring0 的致命异常画 panic 页;用户程序与 int 0x80 暂无(x64 版
 * exec 默认禁用)。
 */
#include "idt64.h"
#include "registers64.h"
#include "io.h"

#include "../mode/fb.h"
#include "../mode/font.h"

void print_panic64(const struct irq_frame64 *r);   /* kernel64.c */
void irq64_dispatch(struct irq_frame64 *r);        /* irq64.c(含 EOI) */

#define IDT64_ENTRIES 256

struct idt_gate64 {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
} __attribute__((packed));

static struct idt_gate64 idt[IDT64_ENTRIES]
    __attribute__((aligned(4096)));

/* 64 位模式 lidt 读 m16:64:limit 在 +0,base 必须紧跟在 +2(不能有 pad) */
static struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtp;

static irq64_handler_t handlers[IDT64_ENTRIES];

extern void *isr64_stub_table[48];

uint64_t idt64_page_base(void)
{
    return (uint64_t)(unsigned long)idt;
}

void idt64_set_gate(int n, uint64_t handler)
{
    idt[n].offset_lo  = (uint16_t)(handler & 0xFFFF);
    idt[n].selector   = 0x08;
    idt[n].ist        = 0;
    idt[n].flags      = 0x8E;       /* present, ring0, interrupt gate */
    idt[n].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].offset_hi  = (uint32_t)(handler >> 32);
    idt[n].zero       = 0;
}

void irq64_register(int vector, irq64_handler_t h)
{
    if (vector >= 0 && vector < IDT64_ENTRIES)
        handlers[vector] = h;
}

int idt_verify(void)
{
    if (idt[14].selector != 0x08 || (idt[14].flags & 0x80) == 0)
        return -1;                  /* Page Fault 门 */
    if (idt[13].selector != 0x08 || (idt[13].flags & 0x80) == 0)
        return -2;                  /* GPF 门 */
    return 0;
}

/* 致命异常(ring0):画 panic 页并停机。
 * 与 i386 版的教训一致:panic 不能依赖控制台,必须直接画帧缓冲。 */
__attribute__((noreturn))
static void panic64(const struct irq_frame64 *r)
{
    static const char *names[32] = {
        "Divide by Zero",          "Debug",
        "Non-Maskable Interrupt",  "Breakpoint",
        "Overflow",                "Out of Bounds",
        "Invalid Opcode",          "No Coprocessor",
        "Double Fault",            "Coprocessor Segment Overrun",
        "Invalid TSS",             "Segment Not Present",
        "Stack Fault",             "General Protection Fault",
        "Page Fault",              "Unknown Interrupt",
        "x87 FPU Error",           "Alignment Check",
        "Machine Check",           "SIMD FP Exception",
        "Virtualization Exception","Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved"
    };
    uint64_t cr2 = 0;
    char b[20];

    extern void ser_puts64(const char *s);

    __asm__ volatile ("movq %%cr2, %0" : "=r"(cr2));

    /* 串口最先输出:若故障在帧缓冲路径,画屏会二次故障,屏幕来不及显示 */
    ser_puts64("\r\n[PANIC] ");
    ser_puts64(names[r->int_no & 31]);
    ser_puts64("\r\n  cr2=0x");
    fmt_hex64(cr2, b);
    ser_puts64(b);
    ser_puts64(" rip=0x");
    fmt_hex64(r->rip, b);
    ser_puts64(b);
    ser_puts64(" cs=0x");
    fmt_hex64(r->cs, b);
    ser_puts64(b);
    ser_puts64(" rsp=0x");
    fmt_hex64(r->rsp, b);
    ser_puts64(b);
    ser_puts64(" err=0x");
    fmt_hex64(r->err_code, b);
    ser_puts64(b);
    ser_puts64("\r\n");

    cli();

    if (fb_ready()) {
        unsigned int white = RGB(0xFF, 0xFF, 0xFF);
        int x = 40, y = 56;
        char b[20];

        fb_fillrect(0, 0, (int)fb.width, (int)fb.height,
                    RGB(0x7A, 0x10, 0x10));
        font_drawtext(x, y, "*** YuanCore PANIC (x86-64) ***", white, 3);
        y += 60;

        font_drawtext(x, y, "exception: ", white, 2);
        font_drawtext(x + 200, y, names[r->int_no & 31], white, 2);
        y += 36;

        font_drawtext(x, y, "cr2    : 0x", white, 2);
        fmt_hex64(cr2, b);
        font_drawtext(x + 200, y, b, white, 2);
        y += 30;

        font_drawtext(x, y, "eip    : 0x", white, 2);
        fmt_hex64(r->rip, b);
        font_drawtext(x + 200, y, b, white, 2);
        y += 30;

        font_drawtext(x, y, "cs     : 0x", white, 2);
        fmt_hex64(r->cs, b);
        font_drawtext(x + 200, y, b, white, 2);
        y += 30;

        font_drawtext(x, y, "rsp    : 0x", white, 2);
        fmt_hex64(r->rsp, b);
        font_drawtext(x + 200, y, b, white, 2);
        y += 30;

        font_drawtext(x, y, "err    : 0x", white, 2);
        fmt_hex64(r->err_code, b);
        font_drawtext(x + 200, y, b, white, 2);
        y += 40;

        /* 内核栈顶 8 个字 */
        {
            uint64_t *sp;
            int i, k;

            __asm__ volatile ("movq %%rsp, %0" : "=r"(sp));
            font_drawtext(x, y, "stack:", white, 2);
            y += 30;
            for (i = 0; i < 8; i++) {
                fmt_hex64(sp[i], b);
                k = i % 4;
                font_drawtext(x + k * 240, y, b, white, 2);
                if (k == 3)
                    y += 30;
            }
            y += 10;
        }

        font_drawtext(x, y, "System halted. Take a photo for debugging.",
                      white, 2);
    }

    for (;;)
        hlt();
}

/* 异常 0-31 与未知向量都进这里;IRQ(32-47) 由 irq64.c 注册分发 */
void x64_irq_dispatch(void *framep)
{
    struct irq_frame64 *r = (struct irq_frame64 *)framep;

    if (r->int_no < 32) {
        if (handlers[r->int_no] != 0)
            handlers[r->int_no](r);
        else
            panic64(r);
        return;
    }

    if (r->int_no >= 32 && r->int_no < 48) {
        irq64_dispatch(r);          /* 查 irq64.c 的表 + EOI */
        return;
    }

    panic64(r);
}

void idt64_init(void)
{
    int i;

    for (i = 0; i < 48; i++)
        idt64_set_gate(i, (uint64_t)(unsigned long)isr64_stub_table[i]);

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint64_t)(unsigned long)idt;
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}
