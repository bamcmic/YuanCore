/* exec.h - 程序加载执行
 *
 * 可执行文件 = 标准 ELF32 ET_EXEC，链接基址 4MB、入口符号 app_main，
 * 内容见 mode/exec.c 顶部注释。程序通过 apps/ycapi.h 调用内核服务。
 */
#ifndef EXEC_H
#define EXEC_H

#define EXEC_OK       0
#define EXEC_NOTFOUND -1
#define EXEC_FORMAT   -2     /* 不是合法的 ELF32 可执行文件 */
#define EXEC_TOOBIG   -3
#define EXEC_ERR      -4
#define EXEC_DISABLED -5     /* 稳定性原因默认禁用（settings exp exec on 启用） */

/* 稳定性闸门：ring3 程序执行历史上两次未定位根因的内核态踩踏
 * （IDT 门被覆盖），因此默认禁用。启用 = settings exp exec on。 */
int  exec_enabled_get(void);
void exec_set_enabled(int on);

/* 从文件系统加载 ELF32 并切到 ring3 执行。
 * 程序 yc_exit 后返回；退出码用 exec_last_status() 取。
 * 注意：与内核同特权级页保护之外没有别的隔离，坏程序会被页错误杀死。 */
int exec_run(const char *path);
int exec_last_status(void);
void exec_set_exit(int code);

#endif /* EXEC_H */
