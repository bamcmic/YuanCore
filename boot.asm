; boot.asm - Multiboot 头部，让 GRUB 认识我们
section .multiboot
align 4
    dd 0x1BADB002      ; 魔数
    dd 0x00            ; 标志位
    dd -(0x1BADB002 + 0x00) ; 校验和

section .text
global start
extern kmain          ; 声明外部 C 函数

start:
    mov esp, stack_top ; 设置栈指针
    push ebx           ; 传递 GRUB 的内存信息结构体
    push eax           ; 传递 GRUB 的魔数
    call kmain         ; 进入 C 世界！
    cli
    hlt

section .bss
align 16
resb 16384            ; 16KB 栈空间
stack_top: