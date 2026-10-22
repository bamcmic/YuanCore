/* usermode.h - ring3 用户态切换接口（双架构）
 *
 * i386:  user_enter 保存内核现场,经 iretd + 用户区跳板进 ring3
 * x64:   user_enter64 保存内核现场,内核态预置用户数据段后 iretq 一步
 *        进入 ring3(长模式无跳板需求,见 x64/switch64.asm)
 * 本函数不返回:程序通过 YC_EXIT 系统调用回到内核(user_exit/64)。
 */
#ifndef USERMODE_H
#define USERMODE_H

#if defined(__x86_64__)

void user_enter64(unsigned long long entry, unsigned long long ustack);
void user_exit64(void) __attribute__((noreturn));

#else

/* 切到 ring3 执行 entry(eax 直传)，用户栈顶 ustack。
 * 本函数不返回：程序通过 YC_EXIT 系统调用回到内核。 */
void user_enter(unsigned int entry, unsigned int ustack);

/* 从用户态系统调用里调用：放弃用户程序，恢复进入前的内核现场。
 * 不返回。 */
void user_exit(void) __attribute__((noreturn));

#endif

#endif /* USERMODE_H */
