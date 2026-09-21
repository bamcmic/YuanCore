/* shell/app_settings.c - 图形化设置程序(双选项卡)
 *
 *   [Theme]         主题预置(5 套,Enter 应用)
 *   [Experimental]  全部实验性选项开关(表在 shell/exp.c,与
 *                   settings exp 命令共用一份数据)
 *
 * 这是用 ui.h 写"程序"的完整范例:一个窗口 + 选项卡 + 按钮列,
 * 方向键/Tab 移动焦点,Enter 应用,Esc 关闭。按住标题栏可拖动。
 */
#include "ui.h"

#include "exp.h"

#include "../mode/theme.h"
#include "../mode/fb.h"
#include "../mode/font.h"

#define PRESET_COUNT 5
#define BTN_H        30
#define BTN_GAP      10
#define TAB_H        26
#define EXP_ROWS     5           /* 与 exp.c 表项数一致(绘制按 exp_count) */

static const char *preset_names[PRESET_COUNT] = {
    "default", "ocean", "sunset", "matrix", "light"
};

static int win_id = -1;
static int page;                    /* 0=Theme 1=Experimental */

static void apply_preset(int idx)
{
    if (idx < 0 || idx >= PRESET_COUNT)
        return;
    theme_preset(preset_names[idx]);
    ui_refresh();               /* 全部窗口 + 桌面 + 终端一起换肤 */
}

static void draw_tabs(int x, int y, int cw)
{
    int half = (cw - 8) / 2;

    /* 当前页的 tab 用高亮色,非当前页用窗口底色 */
    ui_draw_button(x, y, half, TAB_H, "Theme", page == 0);
    ui_draw_button(x + half + 8, y, cw - half - 8, TAB_H,
                   "Experimental", page == 1);

    /* tab 也登记进焦点空间(登记顺序必须与 focus idx 一致) */
    ui_note_button(x, y, half, TAB_H, 0);
    ui_note_button(x + half + 8, y, cw - half - 8, TAB_H, 1);
}

static void on_draw(int w)
{
    int x, y, cw, ch, i;
    int items;                          /* 当前页的条目数 */

    ui_client(w, &x, &y, &cw, &ch);

    items = (page == 0) ? PRESET_COUNT : exp_count();
    ui_focus_count(2 + items + 1);      /* 2 tab + 页内条目 + Close */

    draw_tabs(x, y, cw);
    y += TAB_H + 10;

    if (page == 0) {
        ui_label(x, y, "Pick a theme preset (Enter to apply):");
        y += 24;

        for (i = 0; i < PRESET_COUNT; i++) {
            int by = y + i * (BTN_H + BTN_GAP);

            ui_button(x, by, cw - 90, BTN_H, preset_names[i], 2 + i);

            /* 右侧画当前主题的三个代表色 */
            ui_panel(x + cw - 76, by + 5,  20, BTN_H - 10,
                     theme_get(THEME_TITLEBAR));
            ui_panel(x + cw - 52, by + 5,  20, BTN_H - 10,
                     theme_get(THEME_ACCENT));
            ui_panel(x + cw - 28, by + 5,  20, BTN_H - 10,
                     theme_get(THEME_CONSOLE_BG));
        }
        y += PRESET_COUNT * (BTN_H + BTN_GAP);
    } else {
        ui_label(x, y, "Experimental toggles (runtime only):");
        y += 24;

        for (i = 0; i < exp_count(); i++) {
            const struct exp_option *o = exp_get(i);
            char line[40];
            int n = 0;

            while (o->name[n] != 0) { line[n] = o->name[n]; n++; }
            while (n < 10) { line[n] = ' '; n++; }
            line[n++] = '[';
            line[n++] = o->get() ? 'x' : ' ';
            line[n++] = ']';
            line[n] = '\0';

            ui_button(x, y + i * (BTN_H + BTN_GAP), cw - 90, BTN_H,
                      line, 2 + i);
        }
        y += exp_count() * (BTN_H + BTN_GAP);
    }

    ui_button(x, y + 6, cw - 90, BTN_H, "Close", 2 + items);
}

static void on_key(int w, int key)
{
    int items = (page == 0) ? PRESET_COUNT : exp_count();
    int f = ui_focus();

    switch (key) {
    case KEY_UP:
    case KEY_LEFT:
        ui_focus_prev();
        break;
    case KEY_DOWN:
    case KEY_RIGHT:
        ui_focus_next();
        break;
    case KEY_ENTER:
    case ' ':
        if (f == 0) {                       /* Theme tab */
            page = 0;
            ui_focus_set(2);
            ui_refresh();
            return;
        }
        if (f == 1) {                       /* Experimental tab */
            page = 1;
            ui_focus_set(2);
            ui_refresh();
            return;
        }
        if (f == 2 + items) {               /* Close */
            ui_close(w);
            return;
        }
        if (page == 0)
            apply_preset(f - 2);
        else {
            const struct exp_option *o = exp_get(f - 2);

            if (o != 0) {
                o->set(o->get() ? 0 : 1);   /* 切换开关 */
                ui_refresh();
            }
        }
        break;
    default:
        break;
    }
}

void app_settings_open(void)
{
    if (ui_valid(win_id)) {
        ui_refresh();               /* 已经开着就重绘一遍 */
        return;
    }
    page = 0;
    win_id = ui_open(300, 100, 360,
                     36 + 14 + TAB_H + 10 + 24
                     + EXP_ROWS * (BTN_H + BTN_GAP) + BTN_H + 30 + 14,
                     "Settings", on_draw, on_key);
}
