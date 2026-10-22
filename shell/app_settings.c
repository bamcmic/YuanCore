/* shell/app_settings.c - 图形化设置(侧边选项卡: Theme / Experimental)
 *
 * 布局: 左侧 ui_tabs_side 垂直选项卡条 + 右侧内容区。
 *   [Theme]        主题预置(5 套, Enter 应用)
 *   [Experimental] 实验项(exp.c 表)
 *
 * 变更记录:
 *   - 调色页(Personalize: 23 个主题变量 R/G/B 通道步进)实测无法正常工作,
 *     已整体移除;主题只保留预置切换(稳定路径)。theme_set/theme_save
 *     等底层接口保留供命令行/os 内部使用。
 */
#include "ui.h"

#include "exp.h"

#include "../mode/theme.h"
#include "../mode/fb.h"
#include "../mode/font.h"

#define PRESET_COUNT 5
#define BTN_H        28
#define LINE_H       30
#define TAB_W        110
#define TAB_H        40

static const char *preset_names[PRESET_COUNT] = {
    "default", "ocean", "sunset", "matrix", "light"
};

static const char *const tab_names[2] = {
    "Theme", "Experimental"
};

static int win_id = -1;
static int page;                    /* 0=Theme 1=Experimental */

static void apply_preset(int idx)
{
    if (idx < 0 || idx >= PRESET_COUNT)
        return;
    theme_preset(preset_names[idx]);
    ui_refresh();
}

static void draw_tabs(int x, int y)
{
    ui_tabs_side(x, y, TAB_W, TAB_H, tab_names, 2, page, 0);
}

static void page_theme(int x, int y, int cw)
{
    int i;

    ui_label(x, y, "Pick a theme preset (Enter to apply):");
    y += 24;
    for (i = 0; i < PRESET_COUNT; i++) {
        int by = y + i * LINE_H;

        ui_button(x, by, cw - 90, BTN_H, preset_names[i], 3 + i);
        ui_panel(x + cw - 76, by + 4,  20, BTN_H - 8,
                 theme_get(THEME_TITLEBAR));
        ui_panel(x + cw - 52, by + 4,  20, BTN_H - 8,
                 theme_get(THEME_ACCENT));
        ui_panel(x + cw - 28, by + 4,  20, BTN_H - 8,
                 theme_get(THEME_CONSOLE_BG));
    }
    ui_button(x, y + PRESET_COUNT * LINE_H + 8, cw - 90, BTN_H,
              "Close", 3 + PRESET_COUNT);
}

static void page_exp(int x, int y, int cw)
{
    int i, n = exp_count();

    ui_label(x, y, "Experimental toggles (runtime only):");
    y += 24;
    for (i = 0; i < n; i++) {
        const struct exp_option *o = exp_get(i);
        char line[64];
        int p = 0;

        while (o->name[p] != 0) { line[p] = o->name[p]; p++; }
        while (p < 10) { line[p] = ' '; p++; }
        line[p++] = '[';
        line[p++] = o->get() ? 'x' : ' ';
        line[p++] = ']';
        line[p] = '\0';

        ui_button(x, y + i * LINE_H, cw - 90, BTN_H, line, 3 + i);
    }
    ui_button(x, y + n * LINE_H + 8, cw - 90, BTN_H, "Close", 3 + n);
}

static void on_draw(int w)
{
    int x, y, cw, ch;

    ui_win_begin(w);
    ui_client_size(w, &cw, &ch);
    (void)ch;

    draw_tabs(8, 8);

    /* 内容区 */
    x = TAB_W + 22;
    y = 8;
    cw -= TAB_W + 40;

    if (page == 0)
        page_theme(x, y, cw);
    else
        page_exp(x, y, cw);
}

static int page_items(int pg)
{
    return (pg == 0) ? PRESET_COUNT : exp_count();
}

static void on_key(int w, int key)
{
    int f = ui_focus();
    int last = 3 + page_items(page);

    (void)w;

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
        if (f <= 1) {                  /* 选项卡 */
            page = f;
            ui_focus_set(3);
            ui_refresh();
            return;
        }
        if (f == last) {               /* Close */
            ui_close(win_id);
            return;
        }
        if (page == 0) {
            apply_preset(f - 3);
        } else {
            const struct exp_option *o = exp_get(f - 3);

            if (o != 0) {
                o->set(o->get() ? 0 : 1);
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
        ui_refresh();
        return;
    }
    page = 0;
    win_id = ui_open(220, 60, 540, 480,
                     "Settings", on_draw, on_key);
}
