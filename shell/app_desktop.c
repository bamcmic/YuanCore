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

/* cwd + name 拼出绝对路径（"name" 也可能是 ".."） */
static void path_join(const char *dir, const char *name, char *out, int max)
{
    int n = 0;

    while (dir[n] != 0 && n < max - 1) { out[n] = dir[n]; n++; }
    if (n > 0 && out[n - 1] != '/') {
        out[n++] = '/';
    }
    while (*name != 0 && n < max - 1)
        out[n++] = *name++;
    out[n] = '\0';
}

static void path_parent(const char *dir, char *out, int max)
{
    int n = 0, last = -1;

    while (dir[n] != 0 && n < max - 1) { out[n] = dir[n]; n++; }
    out[n] = '\0';
    if (n > 1) {
        int k = n - 1;

        if (out[k] == '/')
            out[k--] = '\0';
        for (; k > 0; k--) {
            if (out[k] == '/') {
                last = k;
                break;
            }
        }
        if (last > 0)
            out[last] = '\0';
        else {
            out[0] = '/';               /* "/docs" 的父目录就是根 */
            out[1] = '\0';
        }
    }
}

/* cwd 赋值助手（files_enter_row 要用，前置） */
static char files_cwd[80] = "/";

static void scpy_cwd(const char *src)
{
    int n = 0;

    while (src[n] != 0 && n < (int)sizeof files_cwd - 1) {
        files_cwd[n] = src[n];
        n++;
    }
    files_cwd[n] = '\0';
}

/* ---- 文件管理器 ---- */

static int  files_win = -1;
static int  files_sel;              /* 选中行(含 ".." 行) */
static int  files_scroll;
static int  files_view;             /* 0=目录列表 1=文件内容 */
static char view_path[80];
static char view_buf[601];
static int  view_len;

#define FILES_MAX_ROWS 6

/* 目录条目缓存：draw 时填，key 时用（ui 的 draw/key 是两趟调用） */
#define LIST_MAX 8
static char ent_name[LIST_MAX][FS_NAME_MAX + 2];
static int  ent_dir[LIST_MAX];
static int  ent_n;
static int  list_rows;              /* 实际显示的行数(含 "..") */

static void files_load(void)
{
    int i;

    ent_n = 0;
    list_rows = 0;

    if (files_cwd[1] != '\0') {                 /* 非根目录：给 ".." */
        ent_name[0][0] = '.';
        ent_name[0][1] = '.';
        ent_name[0][2] = '\0';
        ent_dir[0] = 1;
        ent_n = 1;
    }

    for (i = 0; ent_n < LIST_MAX; i++) {
        const char *name = fs_list(files_cwd, i, &ent_dir[ent_n], 0);
        int n = 0;

        if (name == 0)
            break;
        while (name[n] != 0 && n < FS_NAME_MAX) {
            ent_name[ent_n][n] = name[n];
            n++;
        }
        ent_name[ent_n][n] = '\0';
        ent_n++;
    }

    list_rows = ent_n;
    if (files_sel >= list_rows)
        files_sel = list_rows - 1;
    if (files_sel < 0)
        files_sel = 0;
    if (files_scroll > files_sel)
        files_scroll = files_sel;
    if (files_scroll > list_rows - 1)
        files_scroll = (list_rows > 0) ? list_rows - 1 : 0;
}

static void files_enter_row(int row)
{
    if (row < 0 || row >= ent_n)
        return;

    if (ent_dir[row] != 0) {
        if (ent_name[row][0] == '.' && ent_name[row][1] == '.') {
            path_parent(files_cwd, files_cwd, (int)sizeof files_cwd);
        } else {
            char next[80];

            path_join(files_cwd, ent_name[row], next, (int)sizeof next);
            scpy_cwd(next);
        }
        files_sel = 0;
        files_scroll = 0;
        files_load();
        ui_refresh();
        return;
    }

    /* 文件：读进查看器（文本，最多 600 字节） */
    path_join(files_cwd, ent_name[row], view_path, (int)sizeof view_path);
    view_len = fs_read(view_path, view_buf, (int)sizeof view_buf - 1);
    if (view_len < 0)
        view_len = 0;
    view_buf[view_len] = '\0';
    files_view = 1;
    ui_refresh();
}

static void files_draw(int win)
{
    int x, y, cw, ch, i;
    int rows_h;

    ui_client(win, &x, &y, &cw, &ch);
    rows_h = (ch - 24 - 44) / 22;
    if (rows_h > FILES_MAX_ROWS) rows_h = FILES_MAX_ROWS;
    if (rows_h < 1) rows_h = 1;

    ui_focus_count(rows_h + 3);

    if (files_view != 0) {
        /* ---- 文件内容视图 ---- */
        int ln = 0, pos = 0;

        ui_label(x, y, view_path);
        y += 24;

        while (ln < rows_h * 2 && pos < view_len) {
            int e = pos;

            while (e < view_len && view_buf[e] != '\n')
                e++;
            view_buf[e] = '\0';
            ui_label(x, y, view_buf + pos);
            y += 20;
            ln++;
            pos = e + 1;
        }

        ui_button(x, y + 6, (cw - 24) / 2, 30, "Back", rows_h);
        ui_button(x + (cw - 24) / 2 + 24, y + 6, (cw - 24) / 2, 30,
                  "Close", rows_h + 1);
        return;
    }

    /* ---- 目录列表视图 ---- */
    ui_label(x, y, files_cwd);
    y += 24;

    for (i = 0; i < rows_h; i++) {
        int row = files_scroll + i;
        char line[FS_NAME_MAX + 4];

        if (row >= list_rows)
            break;

        {
            int n = 0;

            while (ent_name[row][n] != 0) {
                line[n] = ent_name[row][n];
                n++;
            }
            if (ent_dir[row] != 0 && !(n == 2 && ent_name[row][0] == '.'))
                line[n++] = '/';
            line[n] = '\0';
        }

        ui_button(x, y, cw - 8, 20, line, i);
        y += 22;
    }

    /* 底部按钮：Up / Open / Close */
    {
        int third = (cw - 48) / 3;

        ui_button(x, y + 6, third, 30, "Up", rows_h);
        ui_button(x + third + 24, y + 6, third, 30, "Open", rows_h + 1);
        ui_button(x + 2 * third + 48, y + 6, third, 30, "Close",
                  rows_h + 2);
    }
}

static void files_key(int win, int key)
{
    int rows_h;
    int base;

    {
        int x, y, cw, ch;

        ui_client(win, &x, &y, &cw, &ch);
        rows_h = (ch - 24 - 44) / 22;
        if (rows_h > FILES_MAX_ROWS) rows_h = FILES_MAX_ROWS;
        if (rows_h < 1) rows_h = 1;
        base = rows_h;                  /* 按钮的起始 focus 下标 */
    }

    if (files_view != 0) {
        if (key == KEY_ESC || key == KEY_ENTER || key == KEY_BACKSPACE) {
            if (ui_focus() >= base || key == KEY_ESC || key == KEY_BACKSPACE) {
                files_view = 0;         /* Back */
                ui_refresh();
                return;
            }
        }
        return;
    }

    switch (key) {
    case KEY_UP:
        if (files_sel > 0)
            files_sel--;
        if (files_sel < files_scroll)
            files_scroll = files_sel;
        ui_focus_set(files_sel);
        ui_refresh();
        return;
    case KEY_DOWN:
        if (files_sel < list_rows - 1)
            files_sel++;
        if (files_sel >= files_scroll + rows_h)
            files_scroll = files_sel - rows_h + 1;
        ui_focus_set(files_sel);
        ui_refresh();
        return;
    case KEY_ENTER: {
        int f = ui_focus();

        if (f < base) {
            files_enter_row(files_scroll + f);
            return;
        }
        if (f == base) {                /* Up */
            if (files_cwd[1] != '\0') {
                path_parent(files_cwd, files_cwd, (int)sizeof files_cwd);
                files_sel = 0;
                files_scroll = 0;
                files_load();
                ui_refresh();
            }
            return;
        }
        if (f == base + 1) {            /* Open */
            files_enter_row(files_scroll + files_sel);
            return;
        }
        ui_close(win);                  /* Close */
        return;
    }
    default:
        break;
    }
}

void app_files_open(void)
{
    if (ui_valid(files_win)) {
        ui_refresh();
        return;
    }
    files_sel = 0;
    files_scroll = 0;
    files_view = 0;
    files_load();
    files_win = ui_open(260, 80, 440, 420, "Files", files_draw, files_key);
}

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
        font_drawtext(x + 6, y + 4, line, theme_get(THEME_CONSOLE_FG), 1);
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
