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
;
; ring3 程序经 int 0x80 进入内核(向量 128,DPL3 门):
; 帧格式与 ring0 中断一致(长模式 iretq 总是弹 SS:RSP),
; ds/es/fs/gs 保持用户段值但长模式分段寻址失效,内核照常工作。

[bits 64]

extern x64_irq_dispatch

section .text

; 压栈必须逆序(先 r15 后 rax): rax 最后压 = 落在帧底 rsp+0,
; 与 registers64.h 的 irq_frame64 布局互为镜像。正序压会让 C 侧
; r->rax 读到 r15 —— 系统调用号全错且无任何显性症状。
%macro PUSHALL64 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax
%endmacro

%macro POPALL64 0
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
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

; ---- 系统调用门(向量 128,DPL3,由 idt64.c 单独登记) ----
global isr64_int80
isr64_int80:
    push qword 0
    push qword 128
    jmp isr64_common

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
