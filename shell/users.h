/* users.h - 用户账户(第一部分:用户注册环节)
 *
 * 存储: /users/<用户名> 文件, 内容 = 密码。
 * 诚实声明: ramfs 重启即清空, 密码明文存放 -- 玩具级账户系统,
 * 只为打通"注册 -> 登录 -> 会话"链路, 不提供安全性。
 */
#ifndef USERS_H
#define USERS_H

/* 确保 /users 目录存在(fs_ready 之后调用) */
void users_init(void);

/* 注册: 0 成功, -1 参数非法(名 1..15 字节, 字母数字下划线),
 * -2 已存在, -3 fs 写失败 */
int users_add(const char *name, const char *pass);

/* 校验: 0 = 通过, -1 用户不存在, -2 密码不符 */
int users_check(const char *name, const char *pass);

int users_count(void);                 /* /users 下条目数 */
const char *users_get(int idx);        /* 第 idx 个用户名, 越界返回 0 */

const char *users_current(void);       /* 当前登录名, 未登录 0 */
void users_set_current(const char *name);   /* 0 = 登出 */

#endif /* USERS_H */
