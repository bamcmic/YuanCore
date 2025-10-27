/*
 * x86 启动汇编代码
 * 设置基本环境并调用内核主函数
 */

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384 # 16KB 栈空间
stack_top:

.section .text
.global _start
.type _start, @function

_start:
    # 设置栈指针
    mov $stack_top, %esp
    
    # 调用内核主函数
    call kernel_main
    
    # 如果内核主函数返回，进入无限循环
    cli
1:  hlt
    jmp 1b

.size _start, . - _start