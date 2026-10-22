/* x64/gdt64.h - x86-64 全局描述符表 */
#ifndef X64_GDT64_H
#define X64_GDT64_H

#include <stdint.h>

void gdt64_init(void);          /* 建 64 位 GDT(null/kcode/kdata/ucode/udata)并载入 */
void tss64_init(void);          /* 填 TSS64(rsp0=中断栈顶) + 写描述符 + ltr 0x28 */

/* 中断栈金丝雀(shell 主循环巡检,x64 版用独立 32KB 中断栈) */
int  tss_canary_ok(void);

#endif /* X64_GDT64_H */
