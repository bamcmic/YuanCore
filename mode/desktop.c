/* desktop.c - 图形桌面
 *
 * 纯描点实现，不依赖任何 libc。同一份代码既跑在内核里，
 * 也跑在 Windows 侧的离线预览程序里（便于不出 QEMU 就能看效果）。
 */
#include "desktop.h"
#include "fb.h"
#include "font.h"
#include "theme.h"

#define TASKBAR_H   36
#define TITLEBAR_H  36
#define ICON_SIZE   56

static const struct multiboot_info *last_mbi;

/* shell 改主题后重绘需要 mbi 指针，这里存一份 */
const struct multiboot_info *desktop_mbi(void)
{
    return last_mbi;
}


/* ---- 极简字符串/数字工具（freestanding，无 libc） ---- */

static void scpy(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = '\0';
}

static void scat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = '\0';
}

static char *uitoa_dec(unsigned int v, char *out)
{
    char tmp[12];
    int i = 0, j = 0;

    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return out;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = '\0';
    return out;
}

/* ---- 控件 ---- */

/* 窗口外框（标题栏 + 关闭钮 + 客户区），UI 工具包也用它画程序窗口 */
void desktop_draw_window(int x, int y, int w, int h, const char *title)
{
    int bx, by;

    /* 阴影 */
    fb_fillrect(x + 5, y + 5, w, h, RGB(0x00, 0x00, 0x00));
    /* 边框 + 客户区 */
    fb_fillrect(x, y, w, h, theme_get(THEME_WINDOW_BORDER));
    fb_fillrect(x + 1, y + 1, w - 2, h - 2, theme_get(THEME_WINDOW_BG));
    /* 标题栏 */
    fb_fillrect(x + 1, y + 1, w - 2, TITLEBAR_H, theme_get(THEME_TITLEBAR));
    font_drawtext(x + 12, y + 4, title, theme_get(THEME_TITLE_TEXT), 2);

    /* 关闭按钮：20x18，中间画一个真正的 ×（用字体，而不是两条横线） */
    bx = x + w - 32;
    by = y + (TITLEBAR_H - 18) / 2;
    fb_fillrect(bx, by, 20, 18, theme_get(THEME_DANGER));
    font_drawchar(bx + 6, by + 1, 'x', theme_get(THEME_TITLE_TEXT), 1);
}

static void draw_icon(int x, int y, unsigned int color, const char *label)
{
    fb_fillrect(x, y, ICON_SIZE, ICON_SIZE, color);
    fb_rect(x, y, ICON_SIZE, ICON_SIZE, RGB(0xFF, 0xFF, 0xFF));
    /* 图标内部高光，避免纯色块太单调 */
    fb_fillrect(x + 8, y + 8, ICON_SIZE - 16, 6, RGB(0xFF, 0xFF, 0xFF));
    font_drawtext(x, y + ICON_SIZE + 8, label, RGB(0xFF, 0xFF, 0xFF), 1);
}

/* ---- 主绘制 ---- */

void desktop_draw(const struct multiboot_info *mbi)
{
    int w = (int)fb.width;
    int h = (int)fb.height;
    int desk_h = h - TASKBAR_H;

    last_mbi = mbi;

    /* 背景渐变 */
    fb_gradient(theme_get(THEME_BG_TOP), theme_get(THEME_BG_BOTTOM));

    /* 左侧桌面图标 */
    draw_icon(40, 60,  theme_get(THEME_ICON_FILES), "Files");
    draw_icon(40, 150, theme_get(THEME_ICON_TERMINAL), "Terminal");
    draw_icon(40, 240, theme_get(THEME_ICON_SETTINGS), "Settings");
    draw_icon(40, 330, theme_get(THEME_ICON_ABOUT), "About");

    /* 终端窗口外框（内容区由 console_init 填充） */
    desktop_draw_window(SHELL_WIN_X, SHELL_WIN_Y, SHELL_WIN_W, SHELL_WIN_H,
                        "YuanCore Shell");

    /* 窗口底部状态条 */
    fb_fillrect(SHELL_WIN_X + 1,
                SHELL_WIN_Y + SHELL_WIN_H - 1 - 22,
                SHELL_WIN_W - 2, 22, theme_get(THEME_WINDOW_BG));
    font_drawtext(SHELL_WIN_X + 14,
                  SHELL_WIN_Y + SHELL_WIN_H - 22 + 3,
                  "type 'help' for commands", theme_get(THEME_TEXT_DIM), 1);

    /* 底部任务栏 */
    fb_fillrect(0, desk_h, w, TASKBAR_H, theme_get(THEME_TASKBAR));
    fb_hline(0, desk_h, w, theme_get(THEME_TASKBAR_LINE));

    /* 开始按钮 */
    fb_fillrect(10, desk_h + 7, 78, 22, theme_get(THEME_ACCENT));
    font_drawtext(20, desk_h + 14, "Start", theme_get(THEME_TITLE_TEXT), 1);

    /* 任务栏右侧状态 */
    {
        char v[40], t[16];
        scpy(v, "YuanCore 0.2 | ");
        uitoa_dec(fb.width, t);
        scat(v, t);
        scat(v, "x");
        uitoa_dec(fb.height, t);
        scat(v, t);
        scat(v, " @ ");
        uitoa_dec(fb.bpp, t);
        scat(v, t);
        scat(v, "bpp");
        font_drawtext(w - font_text_width(v, 1) - 16, desk_h + 14,
                      v, theme_get(THEME_TEXT_DIM), 1);
    }
}
