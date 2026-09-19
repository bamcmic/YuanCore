; switch.asm - ring0 <-> ring3 切换
;
; user_enter: 保存内核现场 → 构造 iret 框架跳到 ring3。
;   关键点：iret 进 ring3 后 DS/ES/FS/GS 还是 ring0 的 0x10，
;   在 ring3 里用它们一访问内存就 GPF。所以先把 4 个 0x23 写到
;   用户栈顶，iret 进一个放在用户区的 8 字节跳板，由跳板弹出
;   段寄存器再 jmp *eax（eax = 真实入口，iret 不碰通用寄存器）。
;
; user_exit: 程序通过系统调用要求退出时，恢复保存的内核现场返回。
;   注意顺序：user_enter 里 push ebp,ebx,esi,edi 之后才保存 esp，
;   这里必须逆序弹回。

[bits 32]

global user_enter
global user_exit
extern kernel_esp_save

USER_CS equ 0x1B            ; GDT[3] ring3 代码
USER_DS equ 0x23            ; GDT[4] ring3 数据
TRAMPOLINE equ 0x00500000   ; 用户区内的 8 字节跳板（exec.c 拷贝，memmap.h 同值）

section .text

user_enter:                     ; (unsigned int entry, unsigned int ustack)
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [ebp + 8]          ; entry
    mov ecx, [ebp + 12]         ; ustack

    ; 在用户栈顶放 4 个数据段选择子，给跳板 pop 用
    sub ecx, 16
    mov dword [ecx],      USER_DS
    mov dword [ecx + 4],  USER_DS
    mov dword [ecx + 8],  USER_DS
    mov dword [ecx + 12], USER_DS

    ; 保存内核现场（在 iret 之前）
    mov [kernel_esp_save], esp

    ; iret 框架：ss esp eflags cs eip
    push dword USER_DS          ; ss
    push ecx                    ; esp（栈顶 4 个选择子之下）
    pushfd
    push dword USER_CS          ; cs
    push dword TRAMPOLINE       ; eip → 跳板
    iretd

user_exit:
    mov esp, [kernel_esp_save]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .data
kernel_esp_save dd 0

section .note.GNU-stack noalloc noexec nowrite progbits
