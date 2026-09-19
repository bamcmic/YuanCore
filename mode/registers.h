/* registers.h - 中断压栈现场的内存布局
 *
 * 顺序必须与 isr.asm 里的压栈顺序严格一致：
 *   CPU 压入: eip cs eflags (如有特权级变化还有 user_esp ss)
 *   桩压入  : (err_code) int_no
 *   公共例程: pusha 的 8 个寄存器，再 ds es fs gs
 */
#ifndef REGISTERS_H
#define REGISTERS_H

/* 栈上从低到高(与 isr.asm 压栈顺序一致):
 *   pusha:      eax ecx edx ebx esp_dummy ebp esi edi
 *   push 段寄存器: ds es fs gs  → gs 在最低地址
 *   桩:         int_no err_code
 *   CPU:        eip cs eflags [user_esp ss]
 * 因此结构体偏移 0 是 gs 而不是 ds。 */
struct registers {
    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;

    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int esp_dummy;     /* pusha 压入的 esp，无意义 */
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;

    unsigned int int_no;        /* 向量号 */
    unsigned int err_code;      /* 错误码，无则补 0 */

    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
    unsigned int user_esp;      /* 仅特权级切换时存在 */
    unsigned int ss;
};

/* 偏移自检：gs fs es ds 占 0-15，int_no 在 48，eip 在 56 */
_Static_assert(__builtin_offsetof(struct registers, ds) == 3 * 4,
               "segment order must match isr.asm push order");
_Static_assert(__builtin_offsetof(struct registers, int_no) == 12 * 4,
               "int_no offset must match isr.asm push order");
_Static_assert(__builtin_offsetof(struct registers, eip) == 14 * 4,
               "eip offset must match isr.asm push order");

#endif /* REGISTERS_H */
