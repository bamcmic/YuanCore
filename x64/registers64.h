/* x64/registers64.h - x86-64 中断现场布局
 *
 * 与 x64/isr64.asm 的压栈顺序严格互为约束:
 * 桩压入 15 个通用寄存器(rax..r15,按宏顺序)+ int_no + err_code,
 * CPU 自动压入 rip cs rflags rsp ss。
 * 静态断言钉死关键偏移,布局改坏编译期即报错。
 */
#ifndef X64_REGISTERS64_H
#define X64_REGISTERS64_H

#include <stdint.h>

struct irq_frame64 {
    uint64_t rax;               /* 桩压入(低地址,最后压) */
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t int_no;            /* 向量号(桩压入) */
    uint64_t err_code;          /* 错误码(桩压入或 CPU 自动) */

    uint64_t rip;               /* CPU 自动压入 */
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

/* 桩压入区:15 个通用寄存器 + int_no,共 16 个 8 字节,
 * int_no 之后才是 err_code/rip。 */
_Static_assert(__builtin_offsetof(struct irq_frame64, int_no) == 15 * 8,
               "int_no must sit right above the 15 GPRs");
_Static_assert(__builtin_offsetof(struct irq_frame64, rip) == 17 * 8,
               "rip must sit right above int_no/err_code");
_Static_assert(sizeof(struct irq_frame64) == 22 * 8,
               "frame must be exactly 22 quadwords");

#endif /* X64_REGISTERS64_H */
