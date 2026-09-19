/* usermode.h - ring3 用户态切换接口 */
#ifndef USERMODE_H
#define USERMODE_H

/* 切到 ring3 执行 entry(eax 直传)，用户栈顶 ustack。
 * 本函数不返回：程序通过 YC_EXIT 系统调用回到内核。 */
void user_enter(unsigned int entry, unsigned int ustack);

/* 从用户态系统调用里调用：放弃用户程序，恢复进入前的内核现场。
 * 不返回。 */
void user_exit(void) __attribute__((noreturn));

#endif /* USERMODE_H */
