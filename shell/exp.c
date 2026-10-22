/* shell/exp.c - 实验性选项注册表(共享)
 *
 * 当前实验项:
 *   exec     ring3 程序执行(默认关,见 exec.c 稳定性闸门)
 *   drag     窗口拖动
 *   snap     拖动贴边吸附
 *   seconds  任务栏时钟秒行
 *   refresh  全局刷新(约 30fps 整屏重绘,性能开销大,仅诊断/演示用)
 */
#include "exp.h"

#include "ui.h"

#include "../mode/exec.h"
#include "../mode/desktop.h"
#include "../mode/vbe.h"
#include "../mode/fb.h"

/* res1024: 分辨率实验项(off=1280x800 默认, on=1024x768 小屏快刷档) */
static int res1024_on(void)
{
    return fb.width == 1024;
}

static void res1024_set(int on)
{
    if (vbe_set_mode(on ? 1024 : 1280, on ? 768 : 800) == 0)
        ui_refresh();
}

static const struct exp_option opts[] = {
    { "exec",    "ring3 program execution",   exec_enabled_get,       exec_set_enabled     },
    { "refresh", "global redraw ~30fps",      ui_exp_refresh_on,      ui_exp_refresh       },
    { "drag",    "window dragging",           ui_exp_drag_on,         ui_exp_drag          },
    { "snap",    "snap window to screen edge", ui_exp_snap_on,        ui_exp_snap          },
    { "seconds", "clock seconds row",         desktop_exp_seconds_on, desktop_exp_seconds  },
    { "hi60",    "global redraw 60fps (with refresh)", ui_exp_rate60_on, ui_exp_rate60 },
    { "res1024", "1024x768 mode (QEMU std VGA)", res1024_on, res1024_set },
};

#define EXP_N (int)(sizeof(opts) / sizeof(opts[0]))

int exp_count(void)
{
    return EXP_N;
}

const struct exp_option *exp_get(int idx)
{
    if (idx < 0 || idx >= EXP_N)
        return 0;
    return &opts[idx];
}

static int ci_chr(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

int exp_find(const char *name)
{
    int i;

    if (name == 0)
        return -1;
    for (i = 0; i < EXP_N; i++) {
        const char *a = opts[i].name, *b = name;

        while (*a) {
            if (!ci_chr(*a, *b))
                break;
            a++;
            b++;
        }
        if (*a == 0 && *b == 0)
            return i;
    }
    return -1;
}
