/* memmap.h - 物理内存布局约定（PMM / 堆 / 程序加载都以此为准） */
#ifndef MEMMAP_H
#define MEMMAP_H

/* 1M 以上：内核镜像 + PMM 位图（kernel_end 到位图结束，由 pmm.c 圈定） */

/* 程序加载区：exec.c 把可执行文件拷到这里再跳转。
 * 程序必须按这个基址链接（gcc 用 -Ttext-segment=0x400000）。 */
#define MEM_EXEC_BASE  0x00400000u
#define MEM_EXEC_SIZE  0x00040000u     /* 256KB 程序大小上限 */

/* 用户栈顶（用户区末尾，向下生长） */
#define MEM_USER_STACK_TOP 0x00800000u

/* ring3 入口跳板：8 字节，负责在 ring3 里重载段寄存器后跳到入口 */
#define MEM_USER_TRAMPOLINE 0x00500000u

/* 系统调用表：固定地址，程序通过它调用内核服务。
 * 一页就够，PMM 会把这一页标成保留。 */
#define MEM_SYSCALL_BASE 0x00300000u

/* 内核堆：固定位置，不再挤在位图后面（那是程序区了） */
#define MEM_HEAP_BASE  0x00800000u
#define MEM_HEAP_SIZE  0x00400000u     /* 4MB */

#endif /* MEMMAP_H */
