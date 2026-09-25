/* x64/idt64.h - x86-64 中断描述符表 */
#ifndef X64_IDT64_H
#define X64_IDT64_H

#include <stdint.h>

typedef void (*irq64_handler_t)(void *frame);   /* frame = struct irq_frame64 * */

void idt64_init(void);
void idt64_set_gate(int n, uint64_t handler);   /* ring0 中断门 */
void irq64_register(int vector, irq64_handler_t h);   /* 0-255 */

/* 中断/IRQ 分发(isr64.asm 桩调用) */
void x64_irq_dispatch(void *frame);

/* 关键门完整性巡检:0=完好 */
int  idt_verify(void);

/* IDT 所在页基址(供 paging64_set_ro 只读陷阱) */
uint64_t idt64_page_base(void);

/* 64 位十六进制格式化(panic 页用;kernel64.c 提供) */
void fmt_hex64(uint64_t v, char *out);

#endif /* X64_IDT64_H */
