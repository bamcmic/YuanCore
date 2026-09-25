; isr64.asm - x86-64 中断桩
;
; CPU 在长模式中断时自动压入 SS,RSP,RFLAGS,CS,RIP(某些异常再压错误码)。
; 桩压入 15 个通用寄存器 + int_no + (可选补 0 的)err_code,
; 然后以栈指针为参数调用 C 侧 x64_irq_dispatch(irq_frame64*)。
;
; 栈上布局(低->高,与 x64/registers64.h 严格互为约束):
;   rax rcx rdx rbx rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
;   int_no err_code rip cs rflags rsp ss
; (int_no 后压 = 低地址,err_code 先压 = 高地址)

[bits 64]

global isr64_stub_table
extern x64_irq_dispatch

section .text

%macro PUSHALL64 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POPALL64 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

; 无错误码异常:补 0 占位(err_code 先压 = 高地址,int_no 后压 = 低地址)
%macro ISR64_NOERR 1
isr64_%1:
    push qword 0
    push qword %1
    jmp isr64_common
%endmacro

; 有错误码异常:CPU 已自动压入,补 int_no 即可
%macro ISR64_ERR 1
isr64_%1:
    push qword %1
    jmp isr64_common
%endmacro

isr64_common:
    PUSHALL64
    mov rdi, rsp                ; 参数:struct irq_frame64 *
    cld
    call x64_irq_dispatch
    POPALL64
    add rsp, 16                 ; 丢弃 int_no + err_code
    iretq

; ---- 异常 0-31 ----
ISR64_NOERR 0
ISR64_NOERR 1
ISR64_NOERR 2
ISR64_NOERR 3
ISR64_NOERR 4
ISR64_NOERR 5
ISR64_NOERR 6
ISR64_NOERR 7
ISR64_ERR 8                   ; Double Fault
ISR64_NOERR 9
ISR64_ERR 10                  ; Invalid TSS
ISR64_ERR 11                  ; Segment Not Present
ISR64_ERR 12                  ; Stack Fault
ISR64_ERR 13                  ; General Protection Fault
ISR64_ERR 14                  ; Page Fault
ISR64_NOERR 15
ISR64_NOERR 16
ISR64_ERR 17                  ; Alignment Check
ISR64_NOERR 18                ; Machine Check
ISR64_ERR 19                  ; SIMD FP
%assign i 20
%rep 12                       ; 20-31 无错误码
    ISR64_NOERR i
%assign i i+1
%endrep

; ---- IRQ 0-15(向量 32-47) ----
%assign i 32
%rep 16
    ISR64_NOERR i
%assign i i+1
%endrep

; ---- 桩地址表(0-47,64 位地址),C 侧填 IDT ----
section .rodata
global isr64_stub_table
isr64_stub_table:
%assign i 0
%rep 48
    dq isr64_%+i
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
