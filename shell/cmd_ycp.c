/* shell/cmd_ycp.c - YCP 包管理命令
 *
 *   ycp install /apps/xx.ycp   安装(校验 ycmanifest -> 解压 /apps/<name>/)
 *   ycp list                   列出已装应用
 *   ycp remove <name>          卸载
 * 安装/卸载后自动 registry_rescan(), 开始菜单与 apps 命令即时可见。
 */
#include "shell.h"
#include "console.h"

#include "../mode/ycp.h"
#include "registry.h"

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static void cmd_ycp(const char *args)
{
    args = skip_ws(args);

    if (args[0] == 'i' && args[1] == 'n' && args[2] == 's') {
        /* install <path> */
        char path[96];
        int i = 0, p = 0, rc;
        const char *rest = skip_ws(args + 7);

        while (rest[i] && rest[i] != ' ' && i < (int)sizeof path - 1)
            path[p++] = rest[i++];
        path[p] = '\0';
        if (p == 0) {
            console_puts("usage: ycp install <path.ycp>\n");
            return;
        }
        console_puts("[ycp] installing ");
        console_puts(path);
        console_puts(" ...\n");
        rc = ycp_install(path);
        if (rc == 0) {
            console_puts("[ycp] installed ok\n");
            registry_rescan();
        } else {
            console_puts("[ycp] install failed: ");
            if (rc == -1) console_puts("cannot load zip\n");
            else if (rc == -2) console_puts("ycmanifest.json missing\n");
            else if (rc == -3) console_puts("manifest fields invalid\n");
            else if (rc == -4) console_puts("entry binary missing\n");
            else if (rc == -5) console_puts("illegal path or compression\n");
            else console_puts("fs error (rolled back)\n");
        }
        return;
    }

    if (args[0] == 'l') {
        /* list */
        int i, n = ycp_count();

        console_puts("[ycp] installed packages: ");
        {
            char c = (char)('0' + n / 10);
            if (n >= 10) console_putc(c);
            console_putc((char)('0' + n % 10));
        }
        console_puts("\n");
        for (i = 0; i < n; i++) {
            console_puts("  ");
            console_puts(ycp_name(i));
            console_puts("\n");
        }
        return;
    }

    if (args[0] == 'r') {
        /* remove <name> */
        char name[32];
        int i = 0, p = 0;
        const char *rest = skip_ws(args + 6);

        while (rest[i] && rest[i] != ' ' && i < (int)sizeof name - 1)
            name[p++] = rest[i++];
        name[p] = '\0';
        if (p == 0) {
            console_puts("usage: ycp remove <name>\n");
            return;
        }
        if (ycp_remove(name) == 0) {
            console_puts("[ycp] removed: ");
            console_puts(name);
            console_puts("\n");
            registry_rescan();
        } else {
            console_puts("[ycp] not found: ");
            console_puts(name);
            console_puts("\n");
        }
        return;
    }

    console_puts("usage: ycp install <path.ycp> | ycp list | ycp remove <name>\n");
}

void cmd_ycp_init(void)
{
    static const struct shell_command cmds[] = {
        { "ycp", "YCP package: install/list/remove", cmd_ycp },
    };
    shell_register_command(&cmds[0]);
}
