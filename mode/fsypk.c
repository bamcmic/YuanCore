/* mode/fsypk.c - YPK 目录打包容器 + LZSS
 *
 * 布局见 fsypk.h。全部走内核堆临时缓冲,函数返回前释放;
 * fs 单文件上限 256KB = 容器上限,对应原始树约 300KB
 * (最坏全字面量 n + n/8 的压缩上界会先撞 FS_NOSPACE)。
 */
#include "fsypk.h"
#include "heap.h"

/* ---- LZSS 参数 ---- */
#define LZ_N 4096u          /* 滑窗大小(2 的幂) */
#define LZ_MASK (LZ_N - 1u)
#define LZ_F 18u            /* 最大匹配长 */
#define LZ_T 2u             /* 匹配长 < 3 不值得编码 */
#define LZ_HSIZE 4096u
#define LZ_HMASK (LZ_HSIZE - 1u)
#define LZ_CHAIN 64         /* 哈希链搜索步数上限 */

static inline unsigned int lz_hash(const unsigned char *p)
{
    return (((unsigned int)p[0] << 4) ^ ((unsigned int)p[1] << 2)
            ^ (unsigned int)p[2]) & LZ_HMASK;
}

/* 编码。out 容量须 >= n + n/8 + 16。返回压缩长度。 */
static unsigned int lz_encode(const unsigned char *in, unsigned int n,
                              unsigned char *out, unsigned int outcap)
{
    static unsigned char win[LZ_N];
    static int head[LZ_HSIZE];
    static int prev[LZ_N];
    unsigned int wpos = 0, ip = 0, op = 0;
    unsigned char flag = 0;
    unsigned int flagbits = 0, flagpos;
    unsigned int i;

    for (i = 0; i < LZ_HSIZE; i++)
        head[i] = -1;
    for (i = 0; i < LZ_N; i++)
        win[i] = 0;

    flagpos = op++;                       /* 第一块 flag 占位 */
    if (op > outcap)
        return 0;

    while (ip < n) {
        unsigned int best_len = 0, best_dist = 0;

        /* --- 找最长匹配(哈希链) --- */
        if (ip + LZ_T < n) {
            int cur = head[lz_hash(in + ip)];
            int chain = LZ_CHAIN;

            while (cur >= 0 && chain-- > 0) {
                unsigned int dist = wpos - (unsigned int)cur;
                unsigned int maxl, l;

                if (dist == 0 || dist > LZ_N)
                    break;                /* 槽位脏/滑出窗口 */
                maxl = n - ip;
                if (maxl > LZ_F)
                    maxl = LZ_F;
                for (l = 0; l < maxl; l++) {
                    if (win[(wpos + LZ_N - dist + l) & LZ_MASK]
                        != in[ip + l])
                        break;
                }
                if (l > best_len) {
                    best_len = l;
                    best_dist = dist;
                    if (best_len >= LZ_F)
                        break;            /* 已到上限 */
                }
                cur = prev[cur & LZ_MASK];
            }
        }

        if (best_len > LZ_T) {
            /* 匹配对:flag=1,两字节 (len-3)<<4 | (dist-1)>>8, (dist-1)&FF */
            unsigned int d = best_dist - 1u;

            if (op + 2 > outcap)
                break;
            flag = (unsigned char)(flag | (1u << flagbits));
            out[op++] = (unsigned char)(((best_len - LZ_T - 1u) << 4)
                                      | (d >> 8));
            out[op++] = (unsigned char)(d & 0xFFu);
            /* 匹配字节同样要滚入窗口并登记哈希 */
            {
                unsigned int k;

                for (k = 0; k < best_len; k++) {
                    if (ip + 2 < n) {
                        unsigned int h = lz_hash(in + ip);

                        prev[wpos & LZ_MASK] = head[h];
                        head[h] = (int)wpos;
                    }
                    win[wpos & LZ_MASK] = in[ip];
                    wpos++;
                    ip++;
                }
            }
        } else {
            /* 字面量:flag=0,一字节 */
            if (op + 1 > outcap)
                break;
            out[op++] = in[ip];
            if (ip + 2 < n) {
                unsigned int h = lz_hash(in + ip);

                prev[wpos & LZ_MASK] = head[h];
                head[h] = (int)wpos;
            }
            win[wpos & LZ_MASK] = in[ip];
            wpos++;
            ip++;
        }

        flagbits++;
        if (flagbits == 8) {
            out[flagpos] = flag;          /* 本块完成,回填 flag */
            flag = 0;
            flagbits = 0;
            flagpos = op++;
            if (flagpos > outcap)
                break;
        }
    }
    if (flagbits != 0)
        out[flagpos] = flag;
    else
        op--;                             /* 刚 flush 完的空 flag 占位回退 */

    return op;
}

/* 解码。返回写到 out 的字节数(坏流提前截断)。 */
static unsigned int lz_decode(const unsigned char *in, unsigned int n,
                              unsigned char *out, unsigned int outcap)
{
    static unsigned char win[LZ_N];
    unsigned int wpos = 0, ip = 0, op = 0;
    unsigned char flag = 0;
    unsigned int flagbits = 0;
    unsigned int i;

    for (i = 0; i < LZ_N; i++)
        win[i] = 0;

    while (ip < n) {
        if (flagbits == 0) {
            flag = in[ip++];
            flagbits = 8;
        }
        flagbits--;

        if (flag & 1u) {
            unsigned int b0, b1, len, dist, k;

            if (ip + 2 > n)
                break;                    /* 截断流 */
            b0 = in[ip];
            b1 = in[ip + 1];
            ip += 2;
            len  = (b0 >> 4) + 3u;
            dist = (((b0 & 0x0Fu) << 8) | b1) + 1u;
            if (dist > LZ_N)
                break;                    /* 坏数据 */
            for (k = 0; k < len; k++) {
                unsigned char c;

                if (op >= outcap)
                    return op;
                c = win[(wpos + LZ_N - dist) & LZ_MASK];
                out[op++] = c;
                win[wpos & LZ_MASK] = c;
                wpos++;
            }
        } else {
            unsigned char c;

            if (ip >= n)
                break;
            c = in[ip++];
            if (op >= outcap)
                return op;
            out[op++] = c;
            win[wpos & LZ_MASK] = c;
            wpos++;
        }
        flag >>= 1;
    }
    return op;
}

/* ---- 树序列化 ---- */

#define YPK_MAX_TREE (300u * 1024u)     /* 原始流上限(压缩后 < 256KB 文件限) */
#define YPK_PATHMAX  128

/* 递归统计:返回流字节数(total 累加文件数据) */
static unsigned int calc_tree(const char *dirpath, unsigned int *total,
                              int depth)
{
    int idx;
    unsigned int sum = 0;

    if (depth > 6)                        /* 防病态深树 */
        return 0;

    for (idx = 0; ; idx++) {
        int is_dir = 0, size = 0;
        const char *name = fs_list(dirpath, idx, &is_dir, &size);
        char child[YPK_PATHMAX];
        unsigned int nlen = 0;

        if (name == 0)
            break;
        while (name[nlen] != 0 && nlen < FS_NAME_MAX)
            nlen++;
        sum += 6u + nlen;                 /* type(1)+nlen(1)+size(4) + name */

        if (is_dir) {
            unsigned int k = 0;

            while (dirpath[k] != 0 && k < YPK_PATHMAX - 1) {
                child[k] = dirpath[k];
                k++;
            }
            if (k > 0 && child[k - 1] != '/')
                child[k++] = '/';
            {
                unsigned int m = 0;

                while (name[m] != 0 && k < YPK_PATHMAX - 1)
                    child[k++] = name[m++];
            }
            child[k] = '\0';
            sum += calc_tree(child, total, depth + 1);
        } else {
            sum += (unsigned int)size;
            if (total != 0)
                *total += (unsigned int)size;
        }
        if (sum > YPK_MAX_TREE)
            return YPK_MAX_TREE + 1u;     /* 超限快速失败 */
    }
    return sum;
}

/* dirpath + "/name"(辅助,write_tree 用) */
static char cat_path_out[YPK_PATHMAX];
static const char *cat_path(const char *dir, const char *name)
{
    unsigned int k = 0, m = 0;

    while (dir[k] != 0 && k < YPK_PATHMAX - 1) {
        cat_path_out[k] = dir[k];
        k++;
    }
    if (k > 0 && cat_path_out[k - 1] != '/')
        cat_path_out[k++] = '/';
    while (name[m] != 0 && k < YPK_PATHMAX - 1)
        cat_path_out[k++] = name[m++];
    cat_path_out[k] = '\0';
    return cat_path_out;
}

/* 递归写出记录,p 返回推进后的位置 */
static void write_tree(const char *dirpath, unsigned char *p,
                       unsigned int *used, unsigned int cap, int depth)
{
    int idx;

    if (depth > 6)
        return;

    for (idx = 0; ; idx++) {
        int is_dir = 0, size = 0;
        const char *name = fs_list(dirpath, idx, &is_dir, &size);
        char child[YPK_PATHMAX];
        unsigned int nlen = 0, k, m;

        if (name == 0)
            break;
        while (name[nlen] != 0 && nlen < FS_NAME_MAX)
            nlen++;

        if (*used + 6u + nlen > cap)
            return;
        p[(*used)++] = (unsigned char)(is_dir ? 1 : 2);
        p[(*used)++] = (unsigned char)nlen;
        for (k = 0; k < nlen; k++)
            p[(*used)++] = (unsigned char)name[k];
        {
            unsigned int v = (unsigned int)size;

            p[(*used)++] = (unsigned char)(v & 0xFFu);
            p[(*used)++] = (unsigned char)((v >> 8) & 0xFFu);
            p[(*used)++] = (unsigned char)((v >> 16) & 0xFFu);
            p[(*used)++] = (unsigned char)((v >> 24) & 0xFFu);
        }

        if (is_dir) {
            k = 0;
            while (dirpath[k] != 0 && k < YPK_PATHMAX - 1) {
                child[k] = dirpath[k];
                k++;
            }
            if (k > 0 && child[k - 1] != '/')
                child[k++] = '/';
            m = 0;
            while (name[m] != 0 && k < YPK_PATHMAX - 1)
                child[k++] = name[m++];
            child[k] = '\0';
            write_tree(child, p, used, cap, depth + 1);
        } else {
            if (*used + (unsigned int)size > cap)
                return;
            fs_read(cat_path(dirpath, name), p + *used, size);
            *used += (unsigned int)size;
        }
    }
}

/* dirpath + "/name" 组装已在上方 cat_path 完成 */

int fs_ypk_pack(const char *dirpath, const char *out_path)
{
    unsigned int total = 0, raw_len, comp_len, used = 0;
    unsigned char *raw, *comp;
    int rc;

    if (!fs_ready() || dirpath == 0 || out_path == 0)
        return FS_ERR;
    if (!fs_is_dir(dirpath))
        return FS_NOTFOUND;

    raw_len = calc_tree(dirpath, &total, 0);
    if (raw_len > YPK_MAX_TREE)
        return FS_NOSPACE;
    raw_len += 1u;                        /* 结束标记 0xFF */

    raw = kmalloc(raw_len);
    if (raw == 0)
        return FS_NOSPACE;
    comp = kmalloc(raw_len + raw_len / 8u + 16u);
    if (comp == 0) {
        kfree(raw);
        return FS_NOSPACE;
    }

    write_tree(dirpath, raw, &used, raw_len, 0);
    raw[used++] = 0xFF;
    raw_len = used;

    comp_len = lz_encode(raw, raw_len, comp,
                         raw_len + raw_len / 8u + 16u);

    {
        unsigned char head[10];

        head[0] = 'Y'; head[1] = 'P'; head[2] = 'K'; head[3] = '1';
        head[4] = (unsigned char)(raw_len & 0xFFu);
        head[5] = (unsigned char)((raw_len >> 8) & 0xFFu);
        head[6] = (unsigned char)((raw_len >> 16) & 0xFFu);
        head[7] = (unsigned char)((raw_len >> 24) & 0xFFu);
        head[8] = (unsigned char)(comp_len & 0xFFu);
        head[9] = (unsigned char)((comp_len >> 8) & 0xFFu);

        rc = fs_write(out_path, head, (int)sizeof(head));
        if (rc == FS_OK)
            rc = fs_append(out_path, comp, (int)comp_len);
    }

    kfree(raw);
    kfree(comp);
    return rc;
}

int fs_ypk_unpack(const char *ypk_path, const char *dest_dir)
{
    static unsigned char filebuf[FS_MAX_FILE];
    unsigned char *raw;
    unsigned int file_len, raw_len, comp_len, comp_at, op = 0;
    unsigned int dirlen = 0;
    int rc = FS_OK;

    if (!fs_ready() || ypk_path == 0 || dest_dir == 0)
        return FS_ERR;

    file_len = (unsigned int)fs_read(ypk_path, filebuf,
                                     (int)sizeof(filebuf));
    if (file_len < 10u)
        return FS_NOTFOUND;
    if (filebuf[0] != 'Y' || filebuf[1] != 'P' || filebuf[2] != 'K'
        || filebuf[3] != '1')
        return FS_FORMAT;

    raw_len  = (unsigned int)filebuf[4]
             | ((unsigned int)filebuf[5] << 8)
             | ((unsigned int)filebuf[6] << 16)
             | ((unsigned int)filebuf[7] << 24);
    comp_len = (unsigned int)filebuf[8]
             | ((unsigned int)filebuf[9] << 8);
    if (raw_len == 0 || raw_len > YPK_MAX_TREE
        || comp_len != file_len - 10u)
        return FS_FORMAT;

    raw = kmalloc(raw_len);
    if (raw == 0)
        return FS_NOSPACE;

    comp_at = lz_decode(filebuf + 10, comp_len, raw, raw_len);
    if (comp_at < comp_len) {
        kfree(raw);
        return FS_FORMAT;             /* 解压提前截断 */
    }

    while (dest_dir[dirlen] != 0 && dirlen < YPK_PATHMAX - 1)
        dirlen++;

    /* 逐条目重建 */
    while (op < raw_len) {
        unsigned char type = raw[op++];
        unsigned char nlen;
        char name[FS_NAME_MAX + 1];
        char path[YPK_PATHMAX];
        unsigned int k, m, size, data_at;

        if (type == 0xFFu)
            break;
        if (op + 1 > raw_len) {
            rc = FS_FORMAT;
            break;
        }
        nlen = raw[op++];
        if (op + nlen + 4u > raw_len) {
            rc = FS_FORMAT;
            break;
        }
        if (nlen >= sizeof(name))
            nlen = (unsigned char)(sizeof(name) - 1);
        for (k = 0; k < nlen; k++)
            name[k] = (char)raw[op + k];
        name[nlen] = '\0';
        op += nlen;
        size = (unsigned int)raw[op] | ((unsigned int)raw[op + 1] << 8)
             | ((unsigned int)raw[op + 2] << 16)
             | ((unsigned int)raw[op + 3] << 24);
        op += 4u;
        if (op + size > raw_len) {
            rc = FS_FORMAT;
            break;
        }
        data_at = op;
        op += size;                       /* 流指针必须始终按格式推进 */

        /* dest_dir + "/" + name */
        k = 0;
        while (dest_dir[k] != 0 && k < YPK_PATHMAX - 1) {
            path[k] = dest_dir[k];
            k++;
        }
        if (k > 0 && path[k - 1] != '/')
            path[k++] = '/';
        for (m = 0; m < nlen && k < YPK_PATHMAX - 1; m++)
            path[k++] = name[m];
        path[k] = '\0';

        if (type == 1u) {
            fs_create_dir(path);          /* 已存在则跳过 */
        } else {
            int r = fs_write(path, raw + data_at, (int)size);

            if (r != FS_OK && r != FS_NOSPACE) {
                rc = r;
                break;
            }
        }
    }

    kfree(raw);
    return rc;
}
