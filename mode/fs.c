/* fs.c - 内存文件系统
 *
 * 实现：定长条目数组 + parent 索引构成目录树，文件内容在内核堆上。
 * 选这套而不是 FAT/initrd，是因为它零依赖、代码量小、足够演示 VFS 语义；
 * 以后接真实磁盘时，把这层换成块设备后端，接口不用动。
 */
#include "fs.h"
#include "heap.h"

#define FS_MAX_ENTRIES 96

struct fs_node {
    char           name[FS_NAME_MAX];
    int            parent;      /* 父目录条目号，根为 -1 */
    int            is_dir;
    int            used;
    int            size;
    unsigned char *data;        /* 仅文件有 */
};

static struct fs_node nodes[FS_MAX_ENTRIES];
static int fs_up;               /* fs_init() 是否已完成 */

/* ---- 字符串工具 ---- */

static int s_eq(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return a == b;
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int s_len(const char *s)
{
    int n = 0;

    if (s == 0)
        return 0;
    while (s[n])
        n++;
    return n;
}

/* ---- 路径解析 ---- */

/* 解析完整路径 → 条目号；失败 -1 */
static int resolve(const char *path)
{
    int cur = 0;                /* 0 = 根目录 */
    const char *p;
    char comp[FS_NAME_MAX];

    if (!fs_up || path == 0 || path[0] != '/')
        return -1;

    p = path + 1;
    if (*p == 0)
        return cur;             /* 就是根 */

    while (*p) {
        int n = 0, i, found = -1;

        while (*p && *p != '/') {
            if (n < FS_NAME_MAX - 1)
                comp[n++] = *p;
            p++;
        }
        comp[n] = '\0';
        if (*p == '/')
            p++;
        if (n == 0)
            continue;           /* 忽略连续的 '/' */

        if (!nodes[cur].is_dir)
            return -1;

        for (i = 1; i < FS_MAX_ENTRIES; i++) {
            if (nodes[i].used && nodes[i].parent == cur
                && s_eq(nodes[i].name, comp)) {
                found = i;
                break;
            }
        }
        if (found < 0)
            return -1;
        cur = found;
    }
    return cur;
}

/* 把 path 拆成 父目录路径 + 最后一节名字 */
static int split_path(const char *path, char *name, int name_max)
{
    int len, last, i;

    if (path == 0 || path[0] != '/')
        return -1;

    len = s_len(path);
    if (len <= 1)
        return -1;              /* 不能对根操作 */

    last = 0;
    for (i = len - 1; i >= 1; i--) {
        if (path[i] == '/') {
            last = i;
            break;
        }
    }

    /* 去掉结尾多余的 '/' */
    while (len > 1 && path[len - 1] == '/')
        len--;
    for (i = len - 1; i >= 1; i--) {
        if (path[i] == '/') {
            last = i;
            break;
        }
    }

    {
        int n = 0;
        for (i = last + 1; i < len; i++) {
            if (path[i] == '/')
                return -1;
            if (n < name_max - 1)
                name[n++] = path[i];
        }
        name[n] = '\0';
        if (n == 0)
            return -1;
    }

    return last;                /* 返回最后一个 '/' 的下标，0 表示在根下 */
}

static int find_child(int dir, const char *name)
{
    int i;

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (nodes[i].used && nodes[i].parent == dir
            && s_eq(nodes[i].name, name))
            return i;
    }
    return -1;
}

static int parent_of(const char *path, int last_slash)
{
    char dirbuf[64];
    int n, i;

    if (last_slash == 0)
        return 0;               /* 根目录 */

    n = 0;
    for (i = 0; i < last_slash && n < (int)sizeof(dirbuf) - 1; i++)
        dirbuf[n++] = path[i];
    dirbuf[n] = '\0';
    return resolve(dirbuf);
}

static int create_node(const char *path, int is_dir)
{
    char name[FS_NAME_MAX];
    int last, parent, i, slot = -1;

    last = split_path(path, name, FS_NAME_MAX);
    if (last < 0)
        return FS_ERR;

    parent = parent_of(path, last);
    if (parent < 0 || !nodes[parent].is_dir)
        return FS_NOTFOUND;

    if (find_child(parent, name) >= 0)
        return FS_EXISTS;

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (!nodes[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return FS_NOSPACE;

    {
        int n = 0;
        while (name[n] != 0) {
            nodes[slot].name[n] = name[n];
            n++;
        }
        nodes[slot].name[n] = '\0';
    }
    nodes[slot].parent = parent;
    nodes[slot].is_dir = is_dir;
    nodes[slot].used   = 1;
    nodes[slot].size   = 0;
    nodes[slot].data   = 0;
    return slot;
}

/* ---- 公开接口 ---- */

int fs_ready(void) { return fs_up; }

int fs_init(void)
{
    int i;

    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        nodes[i].used = 0;
        nodes[i].data = 0;
    }
    nodes[0].used   = 1;
    nodes[0].is_dir = 1;
    nodes[0].parent = -1;
    nodes[0].name[0] = '/';
    nodes[0].name[1] = '\0';
    fs_up = 1;
    return FS_OK;
}

int fs_exists(const char *path) { return resolve(path) >= 0; }

int fs_is_dir(const char *path)
{
    int n = resolve(path);

    return (n >= 0) ? nodes[n].is_dir : 0;
}

int fs_size(const char *path)
{
    int n = resolve(path);

    if (n < 0 || nodes[n].is_dir)
        return -1;
    return nodes[n].size;
}

int fs_create_file(const char *path)
{
    int n = resolve(path);

    if (n >= 0)
        return FS_EXISTS;
    return create_node(path, 0);
}

int fs_create_dir(const char *path)
{
    int n = resolve(path);

    if (n >= 0)
        return FS_EXISTS;
    return create_node(path, 1);
}

int fs_write(const char *path, const void *data, int len)
{
    int n = resolve(path);
    unsigned char *buf;

    if (len < 0 || len > FS_MAX_FILE)
        return FS_NOSPACE;

    if (n < 0) {
        int r = create_node(path, 0);

        if (r < 0)
            return r;
        n = r;
    }
    if (nodes[n].is_dir)
        return FS_ISDIR;

    buf = 0;
    if (len > 0) {
        int i;
        const unsigned char *src = (const unsigned char *)data;

        buf = kmalloc((unsigned int)len);
        if (buf == 0)
            return FS_NOSPACE;
        for (i = 0; i < len; i++)
            buf[i] = src[i];
    }

    if (nodes[n].data != 0)
        kfree(nodes[n].data);
    nodes[n].data = buf;
    nodes[n].size = len;
    return FS_OK;
}

int fs_append(const char *path, const void *data, int len)
{
    int n = resolve(path);
    int old = 0;
    unsigned char *buf;
    const unsigned char *src = (const unsigned char *)data;
    int i;

    if (len < 0)
        return FS_ERR;
    if (n < 0)
        return fs_write(path, data, len);
    if (nodes[n].is_dir)
        return FS_ISDIR;

    old = nodes[n].size;
    if (old + len > FS_MAX_FILE)
        return FS_NOSPACE;

    buf = kmalloc((unsigned int)(old + len));
    if (buf == 0)
        return FS_NOSPACE;

    for (i = 0; i < old; i++)
        buf[i] = nodes[n].data[i];
    for (i = 0; i < len; i++)
        buf[old + i] = src[i];

    if (nodes[n].data != 0)
        kfree(nodes[n].data);
    nodes[n].data = buf;
    nodes[n].size = old + len;
    return FS_OK;
}

int fs_read(const char *path, void *buf, int max)
{
    int n = resolve(path);
    int i, cnt;
    unsigned char *dst = (unsigned char *)buf;

    if (n < 0)
        return FS_NOTFOUND;
    if (nodes[n].is_dir)
        return FS_ISDIR;

    cnt = nodes[n].size;
    if (cnt > max)
        cnt = max;
    for (i = 0; i < cnt; i++)
        dst[i] = nodes[n].data[i];
    return cnt;
}

int fs_delete(const char *path)
{
    int n = resolve(path);
    int i;

    if (n <= 0)
        return (n == 0) ? FS_ERR : FS_NOTFOUND;

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (nodes[i].used && nodes[i].parent == n)
            return FS_NOTEMPTY;
    }

    if (nodes[n].data != 0) {
        kfree(nodes[n].data);
        nodes[n].data = 0;
    }
    nodes[n].used = 0;
    return FS_OK;
}

const char *fs_list(const char *dir, int idx, int *is_dir, int *size)
{
    int d = resolve(dir);
    int i, k = 0;

    if (d < 0 || !nodes[d].is_dir)
        return 0;

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (nodes[i].used && nodes[i].parent == d) {
            if (k == idx) {
                if (is_dir) *is_dir = nodes[i].is_dir;
                if (size)   *size   = nodes[i].size;
                return nodes[i].name;
            }
            k++;
        }
    }
    return 0;
}

void fs_stats(int *used_entries, int *total_entries, int *bytes_used)
{
    int i, u = 0, b = 0;

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (nodes[i].used) {
            u++;
            b += nodes[i].size;
        }
    }
    if (used_entries) *used_entries = u;
    if (total_entries) *total_entries = FS_MAX_ENTRIES - 1;
    if (bytes_used) *bytes_used = b;
}
