/* shell/registry.c - 应用注册表（/apps/<name>/ycmanifest.json 动态扫描）
 *
 * YCP1 架构: 每个已装应用 = /apps/<name>/ 目录, 内含 ycmanifest.json
 * (name/version/main_dir/entry/author/description) 与 main_dir 文件夹。
 * 启动路径 = /apps/<name>/<main_dir>/<entry>。
 * "apps" 命令每次执行都会重扫, ycp install/remove 后自动重扫。
 */
#include "registry.h"

#include "shell.h"

#include "../mode/exec.h"
#include "../mode/fs.h"
#include "../mode/ycp.h"

#define FS_NAME_MAX 30
#define DYN_MAX     16

struct app_entry {
    char name[FS_NAME_MAX];
    char desc[32];
    char path[64];
};

static struct app_entry dyn_apps[DYN_MAX];
static int dyn_count;

static int (*launcher)(const char *path);

void registry_set_launcher(int (*fn)(const char *path))
{
    launcher = fn;
}

static void copy_str(char *dst, int cap, const char *src)
{
    int i = 0;

    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* 扫描 /apps 下含 ycmanifest.json 的子目录 */
void registry_rescan(void)
{
    int i = 0;

    dyn_count = 0;
    if (!fs_ready())
        return;

    while (dyn_count < DYN_MAX) {
        int is_dir = 0;
        const char *nm = fs_list("/apps", i, &is_dir, 0);
        char man[96], main_dir[32], entry[32], desc[64];
        char path[64];
        int p = 0, k;
        struct app_entry *e;

        if (nm == 0)
            break;
        i++;
        if (!is_dir)
            continue;

        /* /apps/<dir>/ycmanifest.json */
        for (k = 0; k < 6 && "/apps/"[k]; k++)
            man[p++] = "/apps/"[k];
        for (k = 0; nm[k] && p < (int)sizeof man - 20; k++)
            man[p++] = nm[k];
        man[p++] = '/';
        copy_str(man + p, (int)sizeof man - p, "ycmanifest.json");
        if (!fs_exists(man))
            continue;

        e = &dyn_apps[dyn_count];

        {
            char buf[1536];
            int len = fs_read(man, buf, (int)sizeof buf - 1);

            if (len <= 0)
                continue;
            buf[len] = '\0';

            if (!ycp_json_str(buf, "main_dir", main_dir, sizeof main_dir) ||
                !ycp_json_str(buf, "entry", entry, sizeof entry))
                continue;

            if (!ycp_json_str(buf, "description", desc, sizeof desc))
                copy_str(e->desc, (int)sizeof e->desc, "yuancore package");
            else
                copy_str(e->desc, (int)sizeof e->desc, desc);
        }

        copy_str(e->name, (int)sizeof e->name, nm);

        /* 启动路径 /apps/<dir>/<main_dir>/<entry> */
        for (k = 0; k < 6 && "/apps/"[k]; k++)
            path[p++] = "/apps/"[k];
        for (k = 0; nm[k] && p < (int)sizeof path - 2; k++)
            path[p++] = nm[k];
        path[p++] = '/';
        for (k = 0; main_dir[k] && p < (int)sizeof path - 2; k++)
            path[p++] = main_dir[k];
        path[p++] = '/';
        for (k = 0; entry[k] && p < (int)sizeof path - 1; k++)
            path[p++] = entry[k];
        path[p] = '\0';

        if (!fs_exists(path))
            continue;                      /* 清单在, 内容缺失 */
        copy_str(e->path, (int)sizeof e->path, path);

        dyn_count++;
    }
}

void registry_init(void)
{
    registry_rescan();                 /* YCP1: 无内置应用, 扫已装清单 */
}

int registry_count(void)
{
    return dyn_count;
}

const char *registry_name(int idx)
{
    if (idx < 0 || idx >= dyn_count)
        return 0;
    return dyn_apps[idx].name;
}

const char *registry_desc(int idx)
{
    if (idx < 0 || idx >= dyn_count)
        return 0;
    return dyn_apps[idx].desc;
}

const char *registry_path(int idx)
{
    if (idx < 0 || idx >= dyn_count)
        return 0;
    return dyn_apps[idx].path;
}

int registry_search(const char *query, int *out, int max)
{
    int i, n = 0;

    if (query == 0)
        query = "";
    for (i = 0; i < dyn_count && n < max; i++) {
        const char *nm = dyn_apps[i].name;
        int hit = 1;

        if (query[0] != 0) {
            hit = 0;
            while (*nm) {
                const char *a = nm, *b = query;

                while (*a && *b &&
                       (*a | 0x20) == (*b | 0x20)) {
                    a++;
                    b++;
                }
                if (*b == 0) {
                    hit = 1;
                    break;
                }
                nm++;
            }
        }
        if (hit)
            out[n++] = i;
    }
    return n;
}

int registry_launch(int idx)
{
    if (idx < 0 || idx >= dyn_count || launcher == 0)
        return -1;
    return launcher(dyn_apps[idx].path);
}

void registry_command_init(void)
{
    registry_rescan();
}
