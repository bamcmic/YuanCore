/* shell/syscall.h - 程序侧系统调用表的构建 */
#ifndef SHELL_SYSCALL_H
#define SHELL_SYSCALL_H

#include "../mode/registers.h"

void syscall_init(void);                     /* 把服务函数指针写到固定地址 */
void syscall_dispatch(struct registers *r);  /* int 0x80 处理（eax=号） */

#endif /* SHELL_SYSCALL_H */
