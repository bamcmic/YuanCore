/* shell/cmd_exec.c - run 命令：从文件系统启动一个程序
 *
 * 程序是 YCP1 扁平二进制（见 mode/exec.c），按固定基址链接，
 * 通过系统调用表(MEM_SYSCALL_BASE)调用内核服务。
 * 示例程序源码在 apps/，make apps 编译。
 */
#include "shell.h"

#include "console.h"

#include "../mode/exec.h"
#include "../mode/fs.h"

static const char *errstr(int e)
{
    switch (e) {
    case EXEC_NOTFOUND: return "program not found";
    case EXEC_FORMAT:   return "not a valid ELF32 executable";
    case EXEC_TOOBIG:   return "program too large";
    case EXEC_DISABLED: return "disabled for stability - enable with 'settings exp exec on'";
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
        console_puts("usage: run <program>   (try: run hello)\n");
        return;
    }

    /* 路径解析：写了名字就自动去 /apps/ 找，补 .app 后缀再试一次 */
    if (path[0] != '/') {
        char cand[72];
        int n = 0;

        for (; path[n] != 0 && n < 60; n++)
            cand[n] = path[n];
        cand[n] = '\0';

        if (!fs_exists(cand)) {
            int i;

            /* /apps/<name> */
            for (i = 0; i < 6; i++)
                cand[i] = "/apps/"[i];
            for (n = 0; path[n] != 0 && n < 60; n++)
                cand[6 + n] = path[n];
            cand[6 + n] = '\0';
        }
        if (!fs_exists(cand)) {
            int n = 0;

            while (cand[n] != 0 && n < 66)
                n++;
            cand[n] = '.'; cand[n + 1] = 'a'; cand[n + 2] = 'p';
            cand[n + 3] = 'p'; cand[n + 4] = '\0';
        }
        for (n = 0; cand[n] != 0 && n < (int)sizeof path - 1; n++)
            path[n] = cand[n];
        path[n] = '\0';
    }

    console_puts("[exec] loading ");
    console_puts(path);
    console_puts(" ...\n");

    r = exec_run(path);
    if (r != EXEC_OK) {
        console_puts("[exec] failed: ");
        console_puts(errstr(r));
        console_puts("\n");
        if (r == EXEC_NOTFOUND) {
            console_puts("(check with 'ls /apps'; a stale kernel/iso? try: make clean && make run)\n");
        }
        return;
    }
    console_puts("[exec] program returned\n");
}

static const struct shell_command run_cmd = {
    "run", "run a program file (experimental, off by default)", cmd_run
};

void cmd_exec_init(void)
{
    shell_register_command(&run_cmd);
}
