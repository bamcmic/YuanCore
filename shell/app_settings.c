/* shell/app_settings.c - 图形化设置程序
 *
 * 这是用 ui.h 写"程序"的完整范例：一个窗口 + 一列按钮，
 * 方向键/Tab 移动焦点，Enter 应用，Esc 关闭。
 * 由 `settings ui` 命令打开。
 */
#include "ui.h"

#include "../mode/theme.h"
#include "../mode/fb.h"

#define PRESET_COUNT 5
#define BTN_H        30
#define BTN_GAP      10

static const char *preset_names[PRESET_COUNT] = {
    "default", "ocean", "sunset", "matrix", "light"
};

static int win_id = -1;

static void apply_preset(int idx)
{
    if (idx < 0 || idx >= PRESET_COUNT)
        return;
    theme_preset(preset_names[idx]);
    ui_refresh();               /* 全部窗口 + 桌面 + 终端一起换肤 */
}

static void on_draw(int w)
{
    int x, y, cw, ch, i;

    ui_client(w, &x, &y, &cw, &ch);
    ui_focus_count(PRESET_COUNT + 1);

    ui_label(x, y, "Pick a theme preset (Enter to apply):");

    for (i = 0; i < PRESET_COUNT; i++) {
        int by = y + 24 + i * (BTN_H + BTN_GAP);

        ui_button(x, by, cw - 90, BTN_H, preset_names[i], i);

        /* 右侧画当前主题的三个代表色 */
        ui_panel(x + cw - 76, by + 5,  20, BTN_H - 10, theme_get(THEME_TITLEBAR));
        ui_panel(x + cw - 52, by + 5,  20, BTN_H - 10, theme_get(THEME_ACCENT));
        ui_panel(x + cw - 28, by + 5,  20, BTN_H - 10, theme_get(THEME_CONSOLE_BG));
    }

    {
        int by = y + 24 + PRESET_COUNT * (BTN_H + BTN_GAP) + 6;
        ui_button(x, by, cw - 90, BTN_H, "Close", PRESET_COUNT);
        ui_label(x, by + BTN_H + 10, "swatches: titlebar / accent / console bg");
    }
}

static void on_key(int w, int key)
{
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
        if (ui_focus() == PRESET_COUNT)
            ui_close(w);
        else
            apply_preset(ui_focus());
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
    win_id = ui_open(320, 120, 400,
                     24 + 24 + PRESET_COUNT * (BTN_H + BTN_GAP)
                     + BTN_H + 34 + 24,
                     "Settings - Theme", on_draw, on_key);
}
