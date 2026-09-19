/* irq.h - 8259 PIC 重映射与硬件中断分发 */
#ifndef IRQ_H
#define IRQ_H

#include "registers.h"

typedef void (*irq_handler_t)(struct registers *r);

void irq_init(void);
void irq_register(int irq, irq_handler_t h);   /* irq: 0-15 */

void pic_unmask(int irq);                      /* 放行某条 IRQ 线 */
void pic_mask(int irq);

#endif /* IRQ_H */
