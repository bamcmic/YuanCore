/* ycp.c - YCP1 应用包安装器(实现见 ycp.h) */
#include "ycp.h"

#include "zip.h"
#include "fs.h"
#include "heap.h"

#define MANIFEST    "ycmanifest.json"
#define APPS_DIR    "/apps"
#define NAME_MAX    15

/* ---- 迷你 JSON: 顶层 "key": "value" ---- */
int ycp_json_str(const char *js, const char *key, char *out, int max)
{
    int i = 0, k, n;

    if (js == 0 || key == 0)
        return 0;

    while (js[i]) {
        if (js[i] != '"') {
            i++;
            continue;
        }
        /* 候选键起点 */
        for (k = 0; key[k]; k++)
            if (js[i + 1 + k] != key[k])
                break;
        if (key[k] == 0 && js[i + 1 + k] == '"') {
            int p = i + 1 + k + 1;         /* 越过收尾引号 */

            while (js[p] == ' ' || js[p] == '\t' ||
                   js[p] == '\n' || js[p] == '\r')
                p++;
            if (js[p] != ':')
                continue;                  /* 不是键值对, 继续扫 */
            p++;
            while (js[p] == ' ' || js[p] == '\t' ||
                   js[p] == '\n' || js[p] == '\r')
                p++;
            if (js[p] != '"')
                return 0;                  /* 值不是字符串 */
            p++;
            n = 0;
            while (js[p] && js[p] != '"' && n < max - 1)
                out[n++] = js[p++];
            out[n] = '\0';
            return (js[p] == '"') ? 1 : 0;
        }
        i++;
    }
    return 0;
}

/* ---- 工具 ---- */

static int str_nonempty(const char *s)
{
    return s[0] != 0;
}

static int name_ok(const char *s)
{
    int n = 0;

    while (s[n]) {
        char c = s[n];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
        n++;
    }
    return (n >= 1 && n <= NAME_MAX);
}

/* 逐级建目录: "/apps/demo/main" -> 依次建 /apps /apps/demo ... */
static int mkdirs(const char *path)
{
    char buf[96];
    int i = 0, k = 0;

    while (path[k] && k < (int)sizeof buf - 1) {
        buf[k] = path[k];
        k++;
    }
    buf[k] = '\0';

    for (i = 1; buf[i]; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (!fs_exists(buf))
                fs_create_dir(buf);
            buf[i] = '/';
        }
    }
    if (!fs_exists(buf)) {
        fs_create_dir(buf);
        return fs_exists(buf) ? 0 : -1;
    }
    return 0;
}

static int path_safe(const char *name)
{
    int i = 0;

    if (name[0] == '/')
        return 0;
    while (name[i]) {
        if (name[i] == '.' && name[i + 1] == '.')
            return 0;
        i++;
    }
    return 1;
}

/* ---- 已装清单 ---- */

int ycp_count(void)
{
    int i = 0, n = 0, is_dir;
    char man[96];

    if (!fs_ready())
        return 0;
    while (fs_list(APPS_DIR, i, &is_dir, 0) != 0) {
        int p = 0, k;
        const char *nm = fs_list(APPS_DIR, i, &is_dir, 0);

        (void)nm;
        i++;
        /* 拼 /apps/<dir>/ycmanifest.json 并检查存在 */
        {
            const char *d = fs_list(APPS_DIR, i - 1, &is_dir, 0);
            const char *pre = APPS_DIR "/";

            if (d == 0 || !is_dir)
                continue;
            for (k = 0; pre[k]; k++)
                man[p++] = pre[k];
            for (k = 0; d[k]; k++)
                man[p++] = d[k];
            man[p++] = '/';
            for (k = 0; MANIFEST[k]; k++)
                man[p++] = MANIFEST[k];
            man[p] = '\0';
            if (fs_exists(man))
                n++;
        }
    }
    return n;
}

const char *ycp_name(int idx)
{
    int is_dir, seen = 0, i = 0;
    char man[96];

    if (!fs_ready() || idx < 0)
        return 0;
    while (fs_list(APPS_DIR, i, &is_dir, 0) != 0) {
        const char *d;
        int p = 0, k;
        const char *pre = APPS_DIR "/";

        d = fs_list(APPS_DIR, i, &is_dir, 0);
        i++;
        if (d == 0 || !is_dir)
            continue;
        for (k = 0; pre[k]; k++)
            man[p++] = pre[k];
        for (k = 0; d[k]; k++)
            man[p++] = d[k];
        man[p++] = '/';
        for (k = 0; MANIFEST[k]; k++)
            man[p++] = MANIFEST[k];
        man[p] = '\0';
        if (!fs_exists(man))
            continue;
        if (seen == idx)
            return fs_list(APPS_DIR, i - 1, &is_dir, 0);
        seen++;
    }
    return 0;
}

/* ---- 安装 ---- */

int ycp_install(const char *ycp_path)
{
    zip_file zf;
    unsigned char *man_buf = 0;
    char man[1536];
    char name[16], version[16], main_dir[32], entry[32];
    char author[48], desc[64];
    char base[96];
    int mi, mlen, i, rc = 0;
    int have_main = 0;

    if (zip_load(ycp_path, &zf) != 0)
        return -1;

    mi = zip_find(&zf, MANIFEST);
    if (mi < 0) {
        zip_free(&zf);
        return -2;
    }
    mlen = zip_read(&zf, mi, (unsigned char *)man, (int)sizeof man - 1);
    if (mlen <= 0) {
        zip_free(&zf);
        return -2;
    }
    man[mlen] = '\0';

    if (!ycp_json_str(man, "name", name, sizeof name) ||
        !ycp_json_str(man, "version", version, sizeof version) ||
        !ycp_json_str(man, "main_dir", main_dir, sizeof main_dir) ||
        !ycp_json_str(man, "entry", entry, sizeof entry) ||
        !ycp_json_str(man, "author", author, sizeof author) ||
        !ycp_json_str(man, "description", desc, sizeof desc)) {
        zip_free(&zf);
        return -3;
    }
    if (!name_ok(name) || !str_nonempty(version) ||
        !name_ok(main_dir) || !str_nonempty(entry)) {
        zip_free(&zf);
        return -3;
    }

    /* 安装根: /apps/<name>/ (覆盖安装: 先清旧) */
    {
        int p = 0, k;
        const char *pre = APPS_DIR "/";

        for (k = 0; pre[k]; k++)
            base[p++] = pre[k];
        for (k = 0; name[k]; k++)
            base[p++] = name[k];
        base[p] = '\0';
    }
    if (fs_exists(base))
        fs_rmtree(base);
    if (mkdirs(base) != 0) {
        zip_free(&zf);
        return -6;
    }

    /* 整树落盘 */
    for (i = 0; i < zf.count && rc == 0; i++) {
        const zip_entry *e = &zf.entries[i];
        char target[128];
        int tp = 0, k, nl;

        if (!path_safe(e->name)) {
            rc = -5;
            break;
        }
        nl = 0;
        while (e->name[nl])
            nl++;
        if (nl == 0)
            continue;

        for (k = 0; base[k]; k++)
            target[tp++] = base[k];
        target[tp++] = '/';
        for (k = 0; k < nl; k++)
            target[tp++] = e->name[k];
        target[tp] = '\0';

        if (e->name[nl - 1] == '/') {
            /* 目录条目 */
            if (mkdirs(target) != 0)
                rc = -6;
            continue;
        }

        if (e->method != 0) {
            rc = -5;                       /* 规格: 仅 stored */
            break;
        }

        {
            unsigned char *data;
            int sz = (e->size > 262144) ? 262144 : (int)e->size;

            data = kmalloc(e->size ? e->size : 1);
            if (data == 0) {
                rc = -6;
                break;
            }
            if (zip_read(&zf, i, data, (int)e->size) < 0) {
                kfree(data);
                rc = -5;
                break;
            }
            /* 父目录确保存在 */
            {
                char dirb[128];
                int dp = 0, m = 0;

                while (target[m] && m < (int)sizeof dirb - 1) {
                    dirb[m] = target[m];
                    m++;
                }
                dirb[m] = '\0';
                while (m > 0 && dirb[m] != '/')
                    m--;
                dirb[m] = '\0';
                if (m > 0 && !fs_exists(dirb))
                    mkdirs(dirb);
                (void)dp;
            }
            if (fs_write(target, data, sz) != 0 && sz > 0)
                rc = -6;
            kfree(data);
        }

        /* main_dir/<entry> 落盘确认 */
        if (!have_main) {
            char chk[160];
            int cp = 0, k2;

            for (k2 = 0; base[k2]; k2++)
                chk[cp++] = base[k2];
            chk[cp++] = '/';
            for (k2 = 0; main_dir[k2]; k2++)
                chk[cp++] = main_dir[k2];
            chk[cp++] = '/';
            for (k2 = 0; entry[k2]; k2++)
                chk[cp++] = entry[k2];
            chk[cp] = '\0';
            if (fs_exists(chk))
                have_main = 1;
        }
    }

    if (rc == 0 && !have_main)
        rc = -4;                           /* entry 未落盘 */

    if (rc != 0)
        fs_rmtree(base);                   /* 失败回滚 */

    zip_free(&zf);
    (void)man_buf;
    return rc;
}

int ycp_remove(const char *name)
{
    char base[96];
    int p = 0, k;
    const char *pre = APPS_DIR "/";

    if (!name_ok(name))
        return -1;
    for (k = 0; pre[k]; k++)
        base[p++] = pre[k];
    for (k = 0; name[k]; k++)
        base[p++] = name[k];
    base[p] = '\0';

    if (!fs_exists(base))
        return -1;
    fs_rmtree(base);
    return 0;
}
