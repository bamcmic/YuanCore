/* users.c - 用户账户(第一部分)
 * 存储与校验,见 users.h。密码明文/ramfs 易失,不做安全性承诺。
 */
#include "users.h"

#include "../mode/fs.h"

#define USERS_DIR   "/users"
#define NAME_MAX    15

static char cur_user[NAME_MAX + 1];    /* '\0' 开头 = 未登录 */

static int name_ok(const char *s)
{
    int n = 0;

    if (s == 0)
        return 0;
    while (s[n]) {
        char c = s[n];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
        n++;
    }
    return (n >= 1 && n <= NAME_MAX);
}

void users_init(void)
{
    if (!fs_ready())
        return;
    fs_create_dir(USERS_DIR);          /* 已存在时返回值无所谓 */
}

int users_add(const char *name, const char *pass)
{
    char path[32];
    int pl = 0, i;

    if (!name_ok(name) || pass == 0 || pass[0] == '\0')
        return -1;
    if (!fs_ready())
        return -3;

    path[pl++] = '/';
    path[pl++] = 'u';
    path[pl++] = 's';
    path[pl++] = 'e';
    path[pl++] = 'r';
    path[pl++] = 's';
    path[pl++] = '/';
    for (i = 0; name[i]; i++)
        path[pl++] = name[i];
    path[pl] = '\0';

    if (fs_exists(path))
        return -2;
    {
        int len = 0;

        while (pass[len])
            len++;
        if (fs_write(path, pass, len) < 0)
            return -3;
    }
    return 0;
}

int users_check(const char *name, const char *pass)
{
    char path[32];
    char buf[32];
    char real[32];
    int pl = 0, i, len;

    if (!name_ok(name) || pass == 0)
        return -1;
    if (!fs_ready())
        return -1;

    path[pl++] = '/';
    path[pl++] = 'u';
    path[pl++] = 's';
    path[pl++] = 'e';
    path[pl++] = 'r';
    path[pl++] = 's';
    path[pl++] = '/';
    for (i = 0; name[i]; i++)
        path[pl++] = name[i];
    path[pl] = '\0';

    if (!fs_exists(path))
        return -1;
    len = fs_read(path, buf, (int)sizeof buf - 1);
    if (len <= 0)
        return -2;
    buf[len] = '\0';

    /* 去尾空白后比较 */
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' '))
        buf[--len] = '\0';
    for (i = 0; i <= len; i++)
        real[i] = buf[i];
    for (i = 0; ; i++) {
        if (pass[i] != real[i])
            return -2;
        if (pass[i] == '\0')
            return 0;
    }
}

int users_count(void)
{
    int i = 0, n = 0, is_dir;

    if (!fs_ready())
        return 0;
    while (fs_list(USERS_DIR, i, &is_dir, 0) != 0) {
        n++;
        i++;
    }
    return n;
}

const char *users_get(int idx)
{
    int is_dir;

    if (!fs_ready() || idx < 0)
        return 0;
    return fs_list(USERS_DIR, idx, &is_dir, 0);
}

const char *users_current(void)
{
    return cur_user[0] ? cur_user : 0;
}

void users_set_current(const char *name)
{
    int i;

    for (i = 0; i <= NAME_MAX; i++)
        cur_user[i] = 0;
    if (name == 0)
        return;
    for (i = 0; name[i] && i < NAME_MAX; i++)
        cur_user[i] = name[i];
    cur_user[i] = '\0';
}
