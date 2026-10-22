/* fs.c - 内存文件系统
 *
 * 实现：定长条目数组 + parent 索引构成目录树，文件内容在内核堆上。
 * 选这套而不是 FAT/initrd，是因为它零依赖、代码量小、足够演示 VFS 语义；
 * 以后接真实磁盘时，把这层换成块设备后端，接口不用动。
 */
#include "fs.h"
#include "heap.h"
#include "ata.h"

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
    n = create_node(path, 0);
    if (n >= 0)
        fs_sync();
    return n;
}

int fs_create_dir(const char *path)
{
    int n = resolve(path);

    if (n >= 0)
        return FS_EXISTS;
    n = create_node(path, 1);
    if (n >= 0)
        fs_sync();
    return n;
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
    fs_sync();
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
    fs_sync();
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
    fs_sync();
    return FS_OK;
}

static void rmtree_recur(int n)
{
    int i;

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (nodes[i].used && nodes[i].parent == n) {
            if (nodes[i].is_dir)
                rmtree_recur(i);        /* 先深删它的子树 */
            if (nodes[i].data != 0) {
                kfree(nodes[i].data);
                nodes[i].data = 0;
            }
            nodes[i].used = 0;
        }
    }
}

/* 递归删除:文件或整棵目录树。根目录拒绝。 */
int fs_rmtree(const char *path)
{
    int n = resolve(path);

    if (n <= 0)
        return (n == 0) ? FS_ERR : FS_NOTFOUND;
    rmtree_recur(n);                    /* 清空孩子 */
    if (nodes[n].data != 0) {           /* n 自己若是文件,释放内容 */
        kfree(nodes[n].data);
        nodes[n].data = 0;
    }
    nodes[n].used = 0;
    fs_sync();
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

/* ---- 持久化:ATA PIO 镜像(ATA 见 mode/ata.c) ----
 *
 * 磁盘布局(LBA):
 *   0     超级块(magic "YCFS1")
 *   1..12 条目表(96 x 64 字节 = 6144 字节)
 *   13..  数据区(文件内容顺序排布,512 对齐)
 * 写入顺序:数据 -> 条目表 -> 超级块。掉电时最多丢最后一次变更,
 * 且不会出现"超级块有效但条目表是旧的"状态。
 */

#define FS_SUPER_SECTOR  0u
#define FS_TABLE_SECTOR  1u
#define FS_DE_SIZE       64u
#define FS_TABLE_SECTORS ((FS_MAX_ENTRIES * FS_DE_SIZE + 511u) / 512u)
#define FS_DATA_SECTOR   (FS_TABLE_SECTOR + FS_TABLE_SECTORS)

static void fs_zero(void *p, unsigned int n)
{
    unsigned char *d = (unsigned char *)p;

    while (n--)
        *d++ = 0;
}

struct fs_disk_super {
    unsigned char magic[8];       /* "YCFS1" + 0 填充 */
    unsigned int  version;
    unsigned int  entry_count;
    unsigned int  data_sector;
    unsigned int  data_bytes;
    unsigned int  checksum;
} __attribute__((packed));

struct fs_disk_entry {
    char          name[FS_NAME_MAX];
    short         parent;
    unsigned char is_dir;
    unsigned char used;
    unsigned int  size;
    unsigned int  offset;         /* 相对数据区起点(512 对齐) */
    unsigned char pad[FS_DE_SIZE - FS_NAME_MAX - 2 - 1 - 1 - 4 - 4];
} __attribute__((packed));

static int fs_persist;

int fs_persist_enabled(void) { return fs_persist; }

int fs_sync(void)
{
    static unsigned char tbl[FS_MAX_ENTRIES * FS_DE_SIZE];
    static unsigned char sec[512];
    struct fs_disk_super sup;
    unsigned int i, off = 0;
    unsigned int k;

    if (!fs_persist || !ata_ready())
        return -1;

    /* 1) 条目表:重建并累计数据偏移 */
    fs_zero(tbl, sizeof tbl);
    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        struct fs_disk_entry *de =
            (struct fs_disk_entry *)(void *)(tbl + i * FS_DE_SIZE);
        int j;

        if (!nodes[i].used)
            continue;
        for (j = 0; j < FS_NAME_MAX - 1 && nodes[i].name[j]; j++)
            de->name[j] = nodes[i].name[j];
        de->name[j] = '\0';
        de->parent = (short)nodes[i].parent;
        de->is_dir = (unsigned char)(nodes[i].is_dir ? 1 : 0);
        de->used   = 1;
        de->size   = (unsigned int)nodes[i].size;
        if (!nodes[i].is_dir) {
            de->offset = off;
            off += ((unsigned int)nodes[i].size + 511u) & ~511u;
        }
    }

    /* 2) 数据区:逐文件整扇区写(尾部补零) */
    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        struct fs_disk_entry *de =
            (struct fs_disk_entry *)(void *)(tbl + i * FS_DE_SIZE);
        unsigned int lba, full, tail;

        if (!nodes[i].used || nodes[i].is_dir || nodes[i].size == 0)
            continue;
        lba  = FS_DATA_SECTOR + de->offset / 512u;
        full = (unsigned int)nodes[i].size / 512u;
        tail = (unsigned int)nodes[i].size % 512u;

        if (full > 0
            && ata_write_sectors(lba, nodes[i].data, full) != 0)
            return -2;
        if (tail > 0) {
            fs_zero(sec, 512);
            for (k = 0; k < tail; k++)
                sec[k] = nodes[i].data[full * 512u + k];
            if (ata_write_sectors(lba + full, sec, 1) != 0)
                return -2;
        }
    }

    /* 3) 条目表 */
    if (ata_write_sectors(FS_TABLE_SECTOR, tbl, FS_TABLE_SECTORS) != 0)
        return -3;

    /* 4) 超级块最后写 */
    fs_zero(&sup, sizeof sup);
    sup.magic[0] = 'Y';
    sup.magic[1] = 'C';
    sup.magic[2] = 'F';
    sup.magic[3] = 'S';
    sup.magic[4] = '1';
    sup.version     = 1;
    sup.entry_count = FS_MAX_ENTRIES;
    sup.data_sector = FS_DATA_SECTOR;
    sup.data_bytes  = off;
    sup.checksum    = (unsigned int)FS_MAX_ENTRIES + off + FS_DATA_SECTOR;

    fs_zero(sec, 512);
    for (i = 0; i < 512u; i++)
        sec[i] = (i < sizeof sup) ? ((const unsigned char *)&sup)[i] : 0u;
    if (ata_write_sectors(FS_SUPER_SECTOR, sec, 1) != 0)
        return -4;

    return 0;
}

int fs_persist_init(void)
{
    static unsigned char tbl[FS_MAX_ENTRIES * FS_DE_SIZE];
    static unsigned char sec[512];
    struct fs_disk_super *sup = (struct fs_disk_super *)(void *)sec;
    unsigned int i;

    fs_persist = 0;
    if (!ata_ready())
        return -1;                    /* 无盘:纯内存,旧行为不变 */

    if (ata_read_sectors(FS_SUPER_SECTOR, sec, 1) != 0)
        return -1;

    if (sup->magic[0] != 'Y' || sup->magic[1] != 'C'
        || sup->magic[2] != 'F' || sup->magic[3] != 'S'
        || sup->magic[4] != '1'
        || sup->entry_count != FS_MAX_ENTRIES
        || sup->data_sector != FS_DATA_SECTOR) {
        fs_persist = 1;               /* 空盘:用现有内存树,首次写建镜像 */
        return 1;
    }

    if (ata_read_sectors(FS_TABLE_SECTOR, tbl, FS_TABLE_SECTORS) != 0) {
        fs_persist = 1;
        return 1;
    }

    /* 清空内存树(根保留),按条目表重建 */
    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        if (nodes[i].data != 0)
            kfree(nodes[i].data);
        nodes[i].data = 0;
        nodes[i].used = 0;
    }

    for (i = 1; i < FS_MAX_ENTRIES; i++) {
        const struct fs_disk_entry *de =
            (const struct fs_disk_entry *)(const void *)
            (tbl + i * FS_DE_SIZE);
        unsigned int j;

        if (!de->used)
            continue;
        for (j = 0; j < FS_NAME_MAX - 1 && de->name[j]; j++)
            nodes[i].name[j] = de->name[j];
        nodes[i].name[j] = '\0';
        nodes[i].parent = de->parent;
        nodes[i].is_dir = de->is_dir ? 1 : 0;
        nodes[i].size   = (int)de->size;
        nodes[i].data   = 0;

        if (!nodes[i].is_dir && de->size > 0) {
            unsigned int lba = FS_DATA_SECTOR + de->offset / 512u;
            unsigned int full = de->size / 512u;
            unsigned int tail = de->size % 512u;
            unsigned char *dst;

            if (de->size > FS_MAX_FILE) {
                nodes[i].used = 0;
                continue;
            }
            dst = kmalloc(de->size);
            if (dst == 0) {
                nodes[i].used = 0;    /* 堆不足:丢弃该文件 */
                continue;
            }
            if (full > 0
                && ata_read_sectors(lba, dst, full) != 0) {
                kfree(dst);
                nodes[i].used = 0;
                continue;
            }
            if (tail > 0) {
                unsigned int k;

                if (ata_read_sectors(lba + full, sec, 1) != 0) {
                    kfree(dst);
                    nodes[i].used = 0;
                    continue;
                }
                for (k = 0; k < tail; k++)
                    dst[full * 512u + k] = sec[k];
            }
            nodes[i].data = dst;
        }
        nodes[i].used = 1;
    }

    fs_persist = 1;
    return 0;
}

