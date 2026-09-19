/* idt.h - 中断描述符表 */
#ifndef IDT_H
#define IDT_H

#include "registers.h"

/* 注册异常处理回调（int_no 0-31），返回非 0 表示已处理 */
typedef void (*isr_handler_t)(struct registers *r);

void idt_init(void);
void idt_set_gate(int n, unsigned int handler);
void idt_set_gate_user(int n, unsigned int handler);   /* DPL=3，ring3 可触发 */
void isr_register(int no, isr_handler_t h);            /* no: 0-255 */

#endif /* IDT_H */
