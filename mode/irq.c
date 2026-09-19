/* irq.c - 8259 PIC 重映射与硬件中断分发
 *
 * 上电默认 IRQ0-7 落在向量 8-15，会跟 CPU 异常打架，必须重映射到 32-47。
 * 平时只放行时钟(0)、键盘(1)和级联(2)，其余屏蔽，避免杂散中断。
 */
#include "irq.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT  0x11
#define ICW4_8086  0x01
#define PIC_EOI    0x20

#define IRQ_OFFSET 32

static irq_handler_t handlers[16];

void irq_register(int irq, irq_handler_t h)
{
    if (irq >= 0 && irq < 16)
        handlers[irq] = h;
}

static void pic_remap(int offset1, int offset2)
{
    outb(PIC1_CMD, ICW1_INIT);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT);
    io_wait();
    outb(PIC1_DATA, (unsigned char)offset1);   /* 主片向量基址 */
    io_wait();
    outb(PIC2_DATA, (unsigned char)offset2);   /* 从片向量基址 */
    io_wait();
    outb(PIC1_DATA, 0x04);                     /* 从片接在 IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);                     /* 8086 模式 */
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* 屏蔽全部，之后按需放行 */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(int irq)
{
    unsigned short port;
    unsigned char mask;

    if (irq < 0 || irq > 15)
        return;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port) & (unsigned char)~(1u << irq);
    outb(port, mask);
}

void pic_mask(int irq)
{
    unsigned short port;
    unsigned char mask;

    if (irq < 0 || irq > 15)
        return;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port) | (unsigned char)(1u << irq);
    outb(port, mask);
}

void irq_init(void)
{
    pic_remap(IRQ_OFFSET, IRQ_OFFSET + 8);

    /* 放行时钟 IRQ0、键盘 IRQ1、级联 IRQ2、鼠标 IRQ12 */
    pic_unmask(0);
    pic_unmask(1);
    pic_unmask(2);
    pic_unmask(12);
}

/* isr.asm 调用：硬件中断入口 */
void irq_dispatch(struct registers *r)
{
    int irq = (int)r->int_no - IRQ_OFFSET;

    if (irq < 0 || irq > 15)
        return;

    /* 从片的中断要给两片都发 EOI */
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
        outb(PIC1_CMD, PIC_EOI);
    } else {
        outb(PIC1_CMD, PIC_EOI);
    }

    if (handlers[irq] != 0)
        handlers[irq](r);
}
