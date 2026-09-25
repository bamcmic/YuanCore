/* x64/gdt64.h - x86-64 全局描述符表 */
#ifndef X64_GDT64_H
#define X64_GDT64_H

#include <stdint.h>

void gdt64_init(void);          /* 建 64 位 GDT(null/code/data)并载入 */

/* 中断栈金丝雀(shell 主循环巡检,x64 版用独立 32KB 中断栈) */
int  tss_canary_ok(void);

#endif /* X64_GDT64_H */
