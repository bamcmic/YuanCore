/* tss.h - 任务状态段（ring3 中断切栈用） */
#ifndef TSS_H
#define TSS_H

void tss_init(void);      /* 填 TSS、装进 GDT 并 ltr */

/* 中断栈底金丝雀是否完好（0 = 栈被溢出踩过，内核已不可信） */
int tss_canary_ok(void);

#endif /* TSS_H */
