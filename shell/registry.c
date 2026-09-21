/* shell/registry.c - 应用注册表
 *
 * 内置静态表。等文件系统有了"安装包"语义后，可以在这里追加
 * 从 /apps 目录扫描出的动态条目——接口已经按 idx 切好。
 */
#include "registry.h"

#include "shell.h"

#include "../mode/exec.h"

struct app_entry {
    const char *name;
    const char *desc;
    const char *path;
};

static const struct app_entry builtin_apps[] = {
    { "hello",    "demo: counter window",        "/apps/hello.app"    },
    { "closeall", "close every open window",     "/apps/closeall.app" },
};

#define APP_COUNT (int)(sizeof(builtin_apps) / sizeof(builtin_apps[0]))

static int (*launcher)(const char *path);

void registry_set_launcher(int (*fn)(const char *path))
{
    launcher = fn;
}

void registry_init(void)
{
    /* 目前全部是内置条目；将来在此扫描 /apps 目录追加动态注册 */
}

int registry_count(void)
{
    return APP_COUNT;
}

const char *registry_name(int idx)
{
    if (idx < 0 || idx >= APP_COUNT)
        return 0;
    return builtin_apps[idx].name;
}

const char *registry_desc(int idx)
{
    if (idx < 0 || idx >= APP_COUNT)
        return 0;
    return builtin_apps[idx].desc;
}

const char *registry_path(int idx)
{
    if (idx < 0 || idx >= APP_COUNT)
        return 0;
    return builtin_apps[idx].path;
}

static int ci_chr(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int ci_str(const char *a, const char *b)
{
    while (*a) {
        if (!ci_chr(*a, *b))
            return 0;
        a++;
        b++;
    }
    return *b == 0;
}

int registry_search(const char *query, int *out, int max)
{
    int i, n = 0;

    if (query == 0 || query[0] == '\0') {
        for (i = 0; i < APP_COUNT && n < max; i++)
            out[n++] = i;
        return n;
    }

    for (i = 0; i < APP_COUNT && n < max; i++) {
        if (ci_str(builtin_apps[i].name, query)
            || ci_str(builtin_apps[i].desc, query))
            out[n++] = i;
    }
    return n;
}

int registry_launch(int idx)
{
    if (idx < 0 || idx >= APP_COUNT || launcher == 0)
        return -1;
    return launcher(builtin_apps[idx].path);
}

/* ---- apps 命令：列出注册表 ---- */

static void cmd_apps(const char *args)
{
    extern void shell_print(const char *s);
    int i;

    (void)args;
    for (i = 0; i < APP_COUNT; i++) {
        shell_print("  ");
        shell_print(builtin_apps[i].name);
        shell_print("  - ");
        shell_print(builtin_apps[i].desc);
        shell_print("  (");
        shell_print(builtin_apps[i].path);
        shell_print(")\n");
    }
}

static const struct shell_command apps_cmd = {
    "apps", "list installed applications", cmd_apps
};

void registry_command_init(void)
{
    shell_register_command(&apps_cmd);
}
