/* shell/shell.h - 交互式命令行
 *
 * 想加一条新命令，推荐做法：
 *   1) 在 shell/ 下新建 cmd_xxx.c
 *   2) 实现  void cmd_xxx(const char *args);
 *   3) 在本文件下方 declarations 处声明，并在 shell/shell.c 的
 *      register_builtin_commands() 里补一行 shell_register_command(...)
 *
 * shell_register_command() 是公开的，也可以在运行时动态注册。
 */
#ifndef SHELL_H
#define SHELL_H

typedef void (*shell_cmd_fn)(const char *args);

struct shell_command {
    const char  *name;      /* 命令名 */
    const char  *help;      /* help 里显示的一行说明 */
    shell_cmd_fn fn;        /* args 为去掉命令名后的剩余部分(可能带前导空格) */
};

void shell_init(void);
void shell_run(void);                       /* 不返回 */

void shell_register_command(const struct shell_command *cmd);

/* 供命令模块重绘整个 UI（改主题后调用） */
void shell_redraw_ui(void);

/* shell 所在终端窗口的输出接口（命令模块直接用它打印） */
void shell_print(const char *s);

#endif /* SHELL_H */
