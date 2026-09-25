/* x64/irq64.c - PIC 重映射与 IRQ 分发(x86-64)
 *
 * 提供与 i386 版 irq.c 相同签名的 irq_register(),timer/kbd/mouse
 * 零改动复用。分发入口由 idt64.c 的 IRQ 分支调用。
 */
#include <stdint.h>

#include "io.h"
#include "registers64.h"

/* 与 i386 版 registers.h 的 handler 签名兼容:timer/kbd/mouse
 * 收到的是 *指向 32 位布局的指针* —— 它们只读 int_no 等少量字段?
 * 实际上它们完全不读 frame(见各自 callback),因此这里把 64 位
 * frame 指针直接转交即可。 */
struct irq_frame64;

typedef void (*irq64_cb_t)(struct irq_frame64 *frame);

static irq64_cb_t handlers[16];

void irq_register(int irq, void *h)
{
    if (irq >= 0 && irq < 16)
        handlers[irq] = (irq64_cb_t)h;
}

static void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void pic64_init(void)
{
    /* 重映射 PIC 到向量 32-47 */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFF); outb(0xA1, 0xFF);

    /* 放行时钟 IRQ0、键盘 IRQ1、级联 IRQ2、鼠标 IRQ12 */
    outb(0x21, (unsigned char)(~(1u | 2u | 4u)));
    outb(0xA1, (unsigned char)(~(1u << 4)));
}

/* mouse.c 等模块调用的通用接口(i386 版在 mode/irq.c,x64 下补齐同签名) */
void pic_unmask(int irq)
{
    unsigned short port = (unsigned short)((irq < 8) ? 0x21 : 0xA1);
    unsigned char v = (unsigned char)(inb(port) & ~(1u << (irq & 7)));
    outb(port, v);
}

/* idt64.c 的 IRQ 分支(32-47)调用 */
void irq64_dispatch(struct irq_frame64 *r)
{
    int irq = (int)r->int_no - 32;

    if (irq >= 0 && irq < 16) {
        if (handlers[irq] != 0)
            handlers[irq](r);
        pic_eoi(irq);
    }
}
