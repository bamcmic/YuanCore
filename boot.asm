; boot.asm - Multiboot 头部，让 GRUB 认识我们
;
; Multiboot flags:
;   bit 0 (0x1) - 按页对齐加载的模块
;   bit 1 (0x2) - 要求 GRUB 提供内存信息 (mem_lower/mem_upper)
;   bit 2 (0x4) - 要求 GRUB 设置图形模式并提供帧缓冲信息
; 校验和必须满足 magic + flags + checksum == 0
;
; bit 2 置位后，头部在 checksum 之后还要追加 4 个字段：
;   mode_type / width / height / depth
; mode_type = 0 表示线性帧缓冲(图形)，1 表示 EGA 文本。

MBOOT_MAGIC    equ 0x1BADB002
MBOOT_FLAGS    equ 0x00000007
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM
    ; ---- GRUB2 兼容性占位（关键，勿删）----
    ; 规范说 a.out 五字段只在 bit16(0x10000) 置位时才存在，
    ; 但 GRUB2 是按固定偏移读整个 header 结构体的，mode_type 恒在 +0x20。
    ; 不补齐这 20 字节，GRUB 就会把紧随其后的内核代码当 mode_type 读走
    ; —— 实测读到的是 kmain 的 prologue：53 83 EC 08 (push ebx; sub esp,8)，
    ;    于是报 "unsupported graphical mode type 149717843"。
    ; bit16 未置位，GRUB 走 ELF 加载路径，这 5 个字段不会被使用。
    times 5 dd 0        ; header_addr / load_addr / load_end_addr / bss_end_addr / entry_addr
    dd 0                ; mode_type: 0 = 线性图形帧缓冲
    dd 1024             ; width
    dd 768              ; height
    dd 32               ; depth (bits per pixel)

section .text
global start
extern kmain          ; 声明外部 C 函数

start:
    mov esp, stack_top ; 设置栈指针

    ; 遵守 cdecl：参数从右往左压栈，于是 kmain(eax=magic, ebx=mbi)
    push ebx           ; GRUB 的 multiboot info 结构体指针
    push eax           ; GRUB 的魔数 0x2BADB002
    call kmain         ; 进入 C 世界！

    ; kmain 正常情况下不返回；返回则停机
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
resb 16384            ; 16KB 栈空间
stack_top:
