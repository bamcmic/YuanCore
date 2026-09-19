; isr.asm - 异常(0-31)与 IRQ(0-15) 的中断桩
;
; CPU 要求每个中断门指向一段代码：压入中断号(必要时补齐错误码)后
; 跳到公共处理例程。公共例程保存全部寄存器，把栈指针当作
; struct registers* 交给 C，返回后恢复现场 iretd。
;
; IRQ 向量在重映射 PIC 后落在 32-47（0x20-0x2F），避免与 CPU 异常冲突。

[bits 32]

extern isr_dispatch
extern irq_dispatch

global isr_stub_table
global int80_stub

%macro ISR_NOERRCODE 1
isr%1:
    push dword 0                ; 补一个假错误码，保持栈布局一致
    push dword %1               ; 中断号
    jmp  isr_common
%endmacro

%macro ISR_ERRCODE 1
isr%1:
    push dword %1               ; CPU 已压入错误码
    jmp  isr_common
%endmacro

%macro IRQ_STUB 2
irq%1:
    push dword 0
    push dword %2               ; 实际向量号 = 32 + IRQ 号
    jmp  irq_common
%endmacro

section .text

; 关键点：call 之前必须 push esp。
; cdecl 要求第一个参数在栈上；不压的话 C 读到的是段寄存器 gs 的值(0x10)
; 当指针，int_no 变成垃圾 → 不发 EOI → PIC 不再投递中断 → 时钟键盘全部失声。
isr_common:
    pusha                       ; edi esi ebp esp ebx edx ecx eax
    push ds
    push es
    push fs
    push gs
    cld
    push esp                    ; 参数: struct registers *（指向 gs）
    call isr_dispatch
    add  esp, 4                 ; 丢掉参数
    pop  gs
    pop  fs
    pop  es
    pop  ds
    popa
    add  esp, 8                 ; 丢掉 int_no 与 err_code
    iretd

irq_common:
    pusha
    push ds
    push es
    push fs
    push gs
    cld
    push esp
    call irq_dispatch
    add  esp, 4
    pop  gs
    pop  fs
    pop  es
    pop  ds
    popa
    add  esp, 8
    iretd

; ---- CPU 异常 0-31：带错误码的只有 8,10,11,12,13,14,17 ----
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; ---- IRQ 0-15 → 向量 32-47 ----
IRQ_STUB 0,  32
IRQ_STUB 1,  33
IRQ_STUB 2,  34
IRQ_STUB 3,  35
IRQ_STUB 4,  36
IRQ_STUB 5,  37
IRQ_STUB 6,  38
IRQ_STUB 7,  39
IRQ_STUB 8,  40
IRQ_STUB 9,  41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

; ---- 系统调用门：int 0x80（IDT 里 DPL=3，供 ring3 触发） ----
int80_stub:
    push dword 0
    push dword 0x80
    jmp  isr_common

; ---- 桩地址表，C 侧用来填 IDT ----
section .data
align 4
isr_stub_table:
%assign i 0
%rep 32
    dd isr %+ i
%assign i i+1
%endrep
%assign i 0
%rep 16
    dd irq %+ i
%assign i i+1
%endrep

; 消除 "missing .note.GNU-stack" 链接警告
section .note.GNU-stack noalloc noexec nowrite progbits
