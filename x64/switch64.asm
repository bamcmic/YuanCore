; switch64.asm - ring0 <-> ring3 切换(x86-64)
;
; 与 i386 版的最大不同:长模式下内核态可以先把 DS/ES/FS/GS 直接设为
; 用户数据段(0x23),iretq 一步进 ring3 —— 不需要用户区跳板。
; (SS 不能在 CPL0 下装载,由 iretq 从栈上恢复;内核期间 ss 保持 0x10。)
;
; user_enter64(entry, ustack):
;   保存内核现场(被调方保存寄存器 + rsp)到 kernel_rsp_save,
;   设数据段寄存器为 0x20,构造 iretq 帧跳 ring3。不返回。
;
; user_exit64():
;   程序经 int 0x80 要求退出(YC_EXIT)时,从 TSS.rsp0 栈直接恢复
;   保存的内核现场,回到 exec64.c 里 user_enter64 的调用者。
;   丢弃用户帧 —— 不再 iretq 回用户。

[bits 64]

global user_enter64
global user_exit64
global kernel_rsp_save

section .text

; 选择子必须带 RPL=3:iretq 以 CS.RPL 决定新 CPL —— 压裸 0x18 会被判
; DPL != CPL 直接 #GP(0x18);SS 同理要求 RPL=新 CPL。DS/ES/FS/GS 在
; CPL0 装载 DPL3 数据段(含 RPL3)合法(max(CPL,RPL) <= DPL)。
USER_CS equ 0x18 | 3        ; GDT[3] ring3 代码(L=1) + RPL3
USER_DS equ 0x20 | 3        ; GDT[4] ring3 数据 + RPL3

user_enter64:               ; SysV: rdi = entry, rsi = ustack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rel kernel_rsp_save], rsp

    ; 数据段寄存器预置为用户段(CPL0 装载 DPL3 数据段合法;
    ; SS 例外,由 iretq 从帧上恢复)
    mov ax, USER_DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; iretq 帧:ss rsp rflags cs rip
    push qword USER_DS
    push rsi
    pushfq
    or  qword [rsp], 0x200  ; 用户态开中断
    push qword USER_CS
    push rdi
    iretq

user_exit64:
    mov rsp, [rel kernel_rsp_save]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; 数据段寄存器切回内核值(进来时被改成用户段)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; int 0x80 是中断门(IF 已关), 本路径不走 iretq 而是直接 ret
    ; 回 exec64.c, 必须显式恢复 IF —— 否则回到内核后 hlt 永眠
    sti
    ret

section .data
kernel_rsp_save dq 0

section .note.GNU-stack noalloc noexec nowrite progbits
