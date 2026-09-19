/* tss.h - 任务状态段（ring3 中断切栈用） */
#ifndef TSS_H
#define TSS_H

void tss_init(void);      /* 填 TSS、装进 GDT 并 ltr */

#endif /* TSS_H */
