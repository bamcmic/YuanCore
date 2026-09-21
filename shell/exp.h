/* shell/exp.h - 实验性选项注册表(共享)
 *
 * settings exp 命令与图形化设置的"Experimental"选项卡共用这一张表。
 * 新增实验项:在 exp.c 的表里加一行(提供 get/set 即可)。
 * 实验项运行时生效、不持久化,稳定后并入正式设置。
 */
#ifndef SHELL_EXP_H
#define SHELL_EXP_H

struct exp_option {
    const char *name;           /* "exec" */
    const char *desc;           /* "ring3 program execution" */
    int  (*get)(void);
    void (*set)(int on);
};

int  exp_count(void);
const struct exp_option *exp_get(int idx);

/* 按名字找,找不到返回 -1 */
int  exp_find(const char *name);

#endif /* SHELL_EXP_H */
