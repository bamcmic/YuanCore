/* shell/cmd_exec.c - run 命令：从文件系统启动一个程序
 *
 * 程序是 YCP1 扁平二进制（见 mode/exec.c），按固定基址链接，
 * 通过系统调用表(MEM_SYSCALL_BASE)调用内核服务。
 * 示例程序源码在 apps/，make apps 编译。
 */
#include "shell.h"

#include "console.h"

#include "../mode/exec.h"

static const char *errstr(int e)
{
    switch (e) {
    case EXEC_NOTFOUND: return "program not found";
    case EXEC_FORMAT:   return "not a YCP1 executable";
    case EXEC_TOOBIG:   return "program too large";
    default:            return "load error";
    }
}

static void cmd_run(const char *args)
{
    char path[64];
    const char *p = args;
    int r;

    while (*p == ' ')
        p++;
    {
        int n = 0;

        while (p[n] != 0 && p[n] != ' ' && n < (int)sizeof path - 1) {
            path[n] = p[n];
            n++;
        }
        path[n] = '\0';
    }

    if (path[0] == '\0') {
        console_puts("usage: run <program>   (try: run /apps/hello.app)\n");
        return;
    }

    console_puts("[exec] loading ");
    console_puts(path);
    console_puts(" ...\n");

    r = exec_run(path);
    if (r != EXEC_OK) {
        console_puts("[exec] failed: ");
        console_puts(errstr(r));
        console_puts("\n");
        return;
    }
    console_puts("[exec] program returned\n");
}

static const struct shell_command run_cmd = {
    "run", "run a program file (YCP1)", cmd_run
};

void cmd_exec_init(void)
{
    shell_register_command(&run_cmd);
}
