/* shell/app_desktop.c - 桌面点击打开的小程序
 *
 *   app_files_open()      文件管理器：目录导航、打开文本文件
 *   app_about_open()      关于窗口
 *   app_startmenu_open()  开始菜单：检索应用注册表 + 固定入口
 *
 * 都是 ui.h 的普通模态窗口；按住标题栏可拖动（拖动是 ui.c 统一实现的）。
 */
#include "ui.h"

#include "console.h"
#include "registry.h"

#include "../mode/kbd.h"
#include "../mode/fb.h"
#include "../mode/font.h"
#include "../mode/theme.h"
#include "../mode/timer.h"
#include "../mode/fs.h"
#include "../mode/pmm.h"
#include "../mode/io.h"
#include "../mode/desktop.h"

/* ---- 小工具 ---- */

static void fmt_uint(unsigned int v, char *out)
{
    char t[12];
    int i = 0, j = 0;

    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        out[j++] = t[--i];
    out[j] = '\0';
}

static void fmt_uptime(char *out)          /* "uptime: 123 s" */
{
    char v[12];
    const char *p = "uptime: ";
    int k = 0;

    fmt_uint(timer_uptime_sec(), v);
    while (p[k]) { out[k] = p[k]; k++; }
    {
        int m = 0;
        while (v[m]) out[k++] = v[m++];
    }
    out[k++] = ' ';
    out[k++] = 's';
    out[k] = '\0';
}

/* Files 窗口(文件管理器)已迁移到 shell/app_files.c —— v2:
 * 行点击按类型分发(.app 运行/.ypk 解压/文本进编辑器),
 * 新增删除/压缩/解压。app_files_open 由该文件实现。 */
extern void app_files_open(void);

/* ---- 关于 ---- */

static void about_draw(int win)
{
    int x, y, cw, ch;
    char b[48];

    ui_client(win, &x, &y, &cw, &ch);
    ui_focus_count(1);

    ui_label(x, y,      "YuanCore - 32-bit x86 hobby kernel");
    ui_label(x, y + 22, "GRUB | paging | ring3 | int 0x80 | ramfs");

    fmt_uptime(b);
    ui_label(x, y + 44, b);

    fmt_uint(pmm_total_kb(), b);
    ui_label(x, y + 66, b);

    ui_button(x, y + 100, 120, 30, "Close", 0);
}

static void about_key(int win, int key)
{
    if (key == KEY_ENTER)
        ui_close(win);
}

void app_about_open(void)
{
    ui_open(320, 160, 400, 280, "About YuanCore", about_draw, about_key);
}

/* ---- 开始菜单：检索应用注册表 + 固定入口 ---- */

static int menu_win = -1;
static char menu_q[24];
static int  menu_qlen;
static int  menu_match[8];
static int  menu_nmatch;

/* 固定入口在 focus 空间里排在学习结果之后 */
#define MENU_SLOTS 4
#define MENU_FIXED 6          /* Files/Terminal/Settings/About/Reboot/Close */

/* id 与桌面图标一致：0=Files 1=Terminal 2=Settings 3=About 4=Start */
extern void shell_desktop_launch(int id);

static void menu_reboot(void)
{
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);
    for (;;)
        hlt();
}

static void menu_refilter(void)
{
    menu_q[menu_qlen] = '\0';
    menu_nmatch = registry_search(menu_q, menu_match, MENU_SLOTS);
}

static void menu_draw(int win)
{
    int x, y, cw, ch, i;
    int fy;

    ui_client(win, &x, &y, &cw, &ch);
    ui_focus_count(MENU_SLOTS + MENU_FIXED);

    /* 搜索框 */
    ui_panel(x, y, cw, 22, theme_get(THEME_CONSOLE_BG));
    {
        char line[26];
        int n = 0;

        while (menu_q[n] != 0) { line[n] = menu_q[n]; n++; }
        line[n++] = '_';
        line[n] = '\0';
        ui_drawtext(x + 6, y + 4, line, theme_get(THEME_CONSOLE_FG), 1);
    }
    y += 30;

    /* 注册表检索结果 */
    fy = y;
    for (i = 0; i < MENU_SLOTS; i++) {
        if (i < menu_nmatch) {
            char line[FS_NAME_MAX + 2];
            const char *src = registry_name(menu_match[i]);
            int n = 0;

            while (src[n] != 0) { line[n] = src[n]; n++; }
            line[n] = '\0';
            ui_button(x, fy, cw, 28, line, i);
        } else {
            ui_button(x, fy, cw, 28, "", i);
        }
        fy += 34;
    }

    /* 固定入口 */
    {
        static const char *fix[MENU_FIXED] = {
            "Files", "Terminal", "Settings", "About", "Reboot", "Close"
        };

        for (i = 0; i < MENU_FIXED; i++) {
            ui_button(x, fy, cw, 28, fix[i], MENU_SLOTS + i);
            fy += 34;
        }
    }
}

static void menu_key(int win, int key)
{
    int f = ui_focus();

    /* 搜索输入（窗口打开期间打字即检索） */
    if (key >= 32 && key < 127) {
        if (menu_qlen < (int)sizeof menu_q - 1) {
            menu_q[menu_qlen++] = (char)key;
            menu_refilter();
            ui_focus_set(0);
            ui_refresh();
        }
        return;
    }
    if (key == KEY_BACKSPACE) {
        if (menu_qlen > 0) {
            menu_qlen--;
            menu_refilter();
            ui_focus_set(0);
            ui_refresh();
        }
        return;
    }
    if (key == KEY_UP) {
        ui_focus_prev();
        ui_refresh();
        return;
    }
    if (key == KEY_DOWN) {
        ui_focus_next();
        ui_refresh();
        return;
    }
    if (key != KEY_ENTER)
        return;

    ui_close(win);

    if (f < menu_nmatch) {
        int r = registry_launch(menu_match[f]);

        if (r != 0) {
            /* 启动失败不崩溃、不静默：明确告诉用户原因 */
            console_puts("[exec] launch failed (");
            if (r == -5)
                console_puts("disabled for stability - 'settings exp exec on'");
            else
                console_puts("load error");
            console_puts(")\n");
        }
        return;
    }
    if (f >= MENU_SLOTS) {
        switch (f - MENU_SLOTS) {
        case 0: shell_desktop_launch(0); break;
        case 1: shell_desktop_launch(1); break;
        case 2: shell_desktop_launch(2); break;
        case 3: shell_desktop_launch(3); break;
        case 4: menu_reboot(); break;
        default: break;                  /* Close：仅关菜单 */
        }
    }
}

void app_startmenu_open(void)
{
    int w = (int)fb.width;

    if (ui_valid(menu_win)) {
        ui_refresh();
        return;
    }
    menu_qlen = 0;
    menu_refilter();
    menu_win = ui_open(w - TASKBAR_W - 8 - 240, 44, 240,
                       36 + 14 + 30 + MENU_SLOTS * 34 + MENU_FIXED * 34 + 14,
                       "Start", menu_draw, menu_key);
}
