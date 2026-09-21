/* shell/registry.h - 应用注册表
 *
 * 系统里"可启动的应用"的统一名册：开始菜单检索、apps 命令、
 * 用户程序查询(经 int 0x80)都从这里走。
 * 新装一个应用 = 在 registry.c 的内置表里加一行。
 */
#ifndef SHELL_REGISTRY_H
#define SHELL_REGISTRY_H

/* 注册启动器（由 shell.c 注入 exec_run，避免 registry 直接依赖 exec） */
void registry_set_launcher(int (*fn)(const char *path));

void registry_init(void);                 /* 装载内置应用 */

int         registry_count(void);
const char *registry_name(int idx);       /* "hello" */
const char *registry_desc(int idx);       /* "demo window app" */
const char *registry_path(int idx);       /* "/apps/hello.app" */

/* 按子串过滤（大小写不敏感，空串=全部）。
 * out 最多写 max 个下标，返回命中数。 */
int  registry_search(const char *query, int *out, int max);

int  registry_launch(int idx);            /* 经启动器执行，返回 exec 的返回码 */

/* 把 "apps" 命令挂进 shell（shell.c 的 shell_init 里调一次） */
void registry_command_init(void);

#endif /* SHELL_REGISTRY_H */
