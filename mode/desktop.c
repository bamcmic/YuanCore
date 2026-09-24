/* desktop.c - 图形桌面
 *
 * 纯描点实现，不依赖任何 libc。同一份代码既跑在内核里，
 * 也跑在 Windows 侧的离线预览程序里（便于不出 QEMU 就能看效果）。
 *
 * 布局（v0.00.10）：
 *   - 任务栏 = 右侧竖条(TASKBAR_W 宽)，Win 键或点右缘提示条唤起/收起；
 *     Start 在条顶，时钟(HH/MM/SS 竖排)在条底
 *   - YuanCore Shell 终端窗口可关闭(×)，Terminal 图标/菜单重开
 *
 * 点击命中区（desktop_icon_hit / desktop_start_hit / desktop_close_hit）
 * 与绘制用的是同一组常量 —— 改布局时两处自动一致。
 */
#include "desktop.h"
#include "fb.h"
#include "font.h"
#include "theme.h"

#define TITLEBAR_H  36
#define ICON_SIZE   56

/* 图标命中区比图标本体大一圈，点标签也能选中 */
#define ICON_HIT_PAD 6

static const struct multiboot_info *last_mbi;
static desktop_clock_fn clock_provider;

static int taskbar_visible = 1;
static int term_visible = 1;
static int exp_seconds = 1;             /* 实验性：时钟秒显示 */

/* shell 改主题后重绘需要 mbi 指针，这里存一份 */
const struct multiboot_info *desktop_mbi(void)
{
    return last_mbi;
}

void desktop_set_clock_provider(desktop_clock_fn fn)
{
    clock_provider = fn;
}

int desktop_clock_ready(void)
{
    return clock_provider != 0;
}

/* ---- 任务栏 / 终端窗口显隐 ---- */

void desktop_taskbar_toggle(void)
{
    taskbar_visible = !taskbar_visible;
}

int desktop_taskbar_visible(void)
{
    return taskbar_visible;
}

void desktop_terminal_show(int on)
{
    term_visible = (on != 0);
}

int desktop_terminal_visible(void)
{
    return term_visible;
}

/* ---- 实验性选项 ---- */

void desktop_exp_seconds(int on)
{
    exp_seconds = (on != 0);
}

int desktop_exp_seconds_on(void)
{
    return exp_seconds;
}

/* ---- 任务栏几何（绘制与命中共用） ---- */

static int bar_x(void)
{
    return (int)fb.width - TASKBAR_W;
}

/* Start 按钮（条顶） */
static void start_rect(int *x, int *y, int *w, int *h)
{
    if (x) *x = bar_x() + 6;
    if (y) *y = 6;
    if (w) *w = TASKBAR_W - 12;
    if (h) *h = 28;
}

/* 时钟区（条底，竖排；秒显示是实验性开关） */
static void clock_rect(int *x, int *y, int *w, int *h)
{
    int rows = exp_seconds ? 3 : 2;

    if (x) *x = bar_x() + 4;
    if (y) *y = (int)fb.height - 12 - rows * 20;
    if (w) *w = TASKBAR_W - 8;
    if (h) *h = rows * 20;
}

/* ---- 点击命中 ---- */

static const int icon_y[4] = { 60, 150, 240, 330 };
#define ICON_X 40

int desktop_icon_hit(int x, int y)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (x >= ICON_X - ICON_HIT_PAD
            && x < ICON_X + ICON_SIZE + ICON_HIT_PAD
            && y >= icon_y[i] - ICON_HIT_PAD
            && y < icon_y[i] + ICON_SIZE + 24 + ICON_HIT_PAD)
            return i;
    }
    return -1;
}

int desktop_start_hit(int x, int y)
{
    int sx, sy, sw, sh;

    if (taskbar_visible == 0)
        return 0;
    start_rect(&sx, &sy, &sw, &sh);
    return x >= sx && x < sx + sw && y >= sy && y < sy + sh;
}

int desktop_taskbar_edge_hit(int x, int y)
{
    (void)y;
    return taskbar_visible == 0
        && x >= (int)fb.width - TASKBAR_STRIP;
}

int desktop_close_hit(int wx, int wy, int ww, int wh, int px, int py)
{
    int bx = wx + ww - 32;
    int by = wy + (TITLEBAR_H - 18) / 2;

    (void)wh;
    return px >= bx && px < bx + 20 && py >= by && py < by + 18;
}

/* ---- 任务栏时钟（竖排 HH / MM / SS） ---- */

void desktop_draw_clock(void)
{
    char buf[16];
    int cx0, cy0, cw0, ch0;
    int i, rows;

    if (clock_provider == 0 || taskbar_visible == 0)
        return;

    clock_rect(&cx0, &cy0, &cw0, &ch0);
    fb_fillrect(cx0, cy0, cw0, ch0, theme_get(THEME_TASKBAR));

    clock_provider(buf, (int)sizeof buf);

    rows = exp_seconds ? 3 : 2;
    for (i = 0; i < rows; i++) {
        char two[3];

        two[0] = buf[i * 3];
        two[1] = buf[i * 3 + 1];
        two[2] = '\0';
        font_drawtext(cx0 + (cw0 - 16) / 2, cy0 + i * 20 + 4, two,
                      theme_get(THEME_TEXT_DIM), 1);
    }
}

/* ---- 壁纸 ---- */

static void draw_wallpaper(int w, int h)
{
    switch (theme_bg_style()) {
    case THEME_WP_SOLID:
        fb_fillrect(0, 0, w, h, theme_get(THEME_BG_TOP));
        break;

    case THEME_WP_DOTS: {
        /* 渐变底 + 每隔 32px 一颗浅色点，便宜而有质感 */
        unsigned int base = theme_get(THEME_BG_TOP);
        unsigned int dot;
        unsigned int r = (base >> 16) & 0xFF, g = (base >> 8) & 0xFF, b = base & 0xFF;
        int x, y;

        r = (r + 0x30 > 0xFF) ? 0xFF : r + 0x30;
        g = (g + 0x30 > 0xFF) ? 0xFF : g + 0x30;
        b = (b + 0x30 > 0xFF) ? 0xFF : b + 0x30;
        dot = RGB(r, g, b);

        fb_gradient(theme_get(THEME_BG_TOP), theme_get(THEME_BG_BOTTOM));
        for (y = 16; y < h; y += 32)
            for (x = 16; x < w; x += 32)
                fb_fillrect(x, y, 2, 2, dot);
        break;
    }

    default:                                /* THEME_WP_GRADIENT */
        fb_gradient(theme_get(THEME_BG_TOP), theme_get(THEME_BG_BOTTOM));
        break;
    }
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

/* ---- 任务栏绘制（整条或局部，两处共用） ---- */

static void draw_taskbar(int W, int H)
{
    int sx, sy, sw, sh;

    (void)W;
    fb_fillrect(bar_x(), 0, TASKBAR_W, H, theme_get(THEME_TASKBAR));
    fb_vline(bar_x(), 0, H, theme_get(THEME_TASKBAR_LINE));

    start_rect(&sx, &sy, &sw, &sh);
    fb_fillrect(sx, sy, sw, sh, theme_get(THEME_ACCENT));
    /* "Start" 40px 宽，按钮 36px —— 居中画，允许压过按钮边 2px */
    font_drawtext(sx + (sw - 40) / 2, sy + 8, "Start",
                  theme_get(THEME_TITLE_TEXT), 1);

    desktop_draw_clock();
}

static void draw_taskbar_strip(int W, int H)
{
    fb_fillrect(W - TASKBAR_STRIP, 0, TASKBAR_STRIP, H,
                theme_get(THEME_ACCENT));
}

/* ---- 合成器底层：按矩形重画桌面上"属于桌面层"的内容 ----
 *
 * 只负责桌面层(壁纸/图标/终端窗口框/任务栏)，不含控制台内容
 * (console.c 自己有 console_repaint_region)和窗口/光标(ui.c 的层)。
 */

static int rects_intersect(int ax, int ay, int aw, int ah,
                           int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* 填充 band 与 rect 的交集——局部重画的一切填充都必须走这个，
 * 直接拿光标矩形当填充范围会越界涂到别的层上（碎块教训）。 */
static void fill_clip(int ax, int ay, int aw, int ah,
                      int bx, int by, int bw, int bh, unsigned int color)
{
    int x0 = (ax > bx) ? ax : bx;
    int y0 = (ay > by) ? ay : by;
    int x1 = (ax + aw < bx + bw) ? (ax + aw) : (bx + bw);
    int y1 = (ay + ah < by + bh) ? (ay + ah) : (by + bh);

    if (x1 > x0 && y1 > y0)
        fb_fillrect(x0, y0, x1 - x0, y1 - y0, color);
}

void desktop_repaint_region(int x, int y, int w, int h)
{
    int W = (int)fb.width;
    int H = (int)fb.height;
    int i;
    int wall_w;                 /* 壁纸实际绘制宽度(给任务栏让位) */

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0)
        return;

    /* z 序与 desktop_draw(全屏)严格一致：壁纸 → 图标 → 终端框 → 任务栏。
     * 教训：曾把任务栏带放在壁纸之前，而壁纸 hline 按整个矩形宽度画、
     * 没给任务栏让位 —— 光标一经过右缘，任务栏就被壁纸盖出色块(拖块)。 */

    /* --- 壁纸：最底层，右缘为任务栏(或提示条)让位 --- */
    {
        int lim = (taskbar_visible != 0) ? bar_x()
                                         : (W - TASKBAR_STRIP);
        int style = theme_bg_style();

        wall_w = (x + w > lim) ? (lim - x) : w;
        if (wall_w > 0) {
            if (style == THEME_WP_SOLID) {
                fb_fillrect(x, y, wall_w, h, theme_get(THEME_BG_TOP));
            } else {
                unsigned int top = theme_get(THEME_BG_TOP);
                unsigned int bot = theme_get(THEME_BG_BOTTOM);
                unsigned int dot;
                unsigned int r8 = (top >> 16) & 0xFF, g8 = (top >> 8) & 0xFF;
                unsigned int b8 = top & 0xFF;
                int r;

                r8 = (r8 + 0x30 > 0xFF) ? 0xFF : r8 + 0x30;
                g8 = (g8 + 0x30 > 0xFF) ? 0xFF : g8 + 0x30;
                b8 = (b8 + 0x30 > 0xFF) ? 0xFF : b8 + 0x30;
                dot = RGB(r8, g8, b8);

                for (r = y; r < y + h; r++) {
                    unsigned int tt = (unsigned int)r * 256 / (unsigned int)H;
                    unsigned int rr = ((top >> 16) & 0xFF) * (255 - tt) / 256
                                    + ((bot >> 16) & 0xFF) * tt / 256;
                    unsigned int gg = ((top >> 8) & 0xFF) * (255 - tt) / 256
                                    + ((bot >> 8) & 0xFF) * tt / 256;
                    unsigned int bb = (top & 0xFF) * (255 - tt) / 256
                                    + (bot & 0xFF) * tt / 256;

                    fb_hline(x, r, wall_w, RGB(rr, gg, bb));
                }
                if (style == THEME_WP_DOTS) {
                    for (r = (y / 32) * 32 + 16; r < y + h; r += 32) {
                        int cx0 = (x / 32) * 32 + 16;
                        int cx1 = x + wall_w;

                        for (; cx0 < cx1; cx0 += 32)
                            fb_fillrect(cx0, r, 2, 2, dot);
                    }
                }
            }
        }
    }

    /* 图标(含标签) */
    {
        static const int iy[4] = { 60, 150, 240, 330 };
        static const char *lb[4] = { "Files", "Terminal", "Settings", "About" };
        unsigned int ic[4];

        ic[0] = theme_get(THEME_ICON_FILES);
        ic[1] = theme_get(THEME_ICON_TERMINAL);
        ic[2] = theme_get(THEME_ICON_SETTINGS);
        ic[3] = theme_get(THEME_ICON_ABOUT);

        for (i = 0; i < 4; i++) {
            if (rects_intersect(x, y, w, h,
                                ICON_X, iy[i], ICON_SIZE, ICON_SIZE + 24))
                draw_icon(ICON_X, iy[i], ic[i], lb[i]);
        }
    }

    /* 终端窗口：可关闭。隐藏时这块就只是壁纸。 */
    if (term_visible != 0) {
        int X = SHELL_WIN_X, Y = SHELL_WIN_Y;
        int Wn = SHELL_WIN_W, Hn = SHELL_WIN_H;

        /* 阴影(窗口右侧与下侧各 5px) */
        fill_clip(X + Wn, Y + 5, 5, Hn - 5, x, y, w, h, RGB(0, 0, 0));
        fill_clip(X + 5, Y + Hn, Wn, 5, x, y, w, h, RGB(0, 0, 0));

        /* 客户区底色 = 窗口内整个矩形(后续带再盖上去) */
        fill_clip(X + 1, Y + 1, Wn - 2, Hn - 2, x, y, w, h,
                  theme_get(THEME_WINDOW_BG));

        /* 1px 边框 */
        fill_clip(X, Y, 1, Hn, x, y, w, h, theme_get(THEME_WINDOW_BORDER));
        fill_clip(X + Wn - 1, Y, 1, Hn, x, y, w, h,
                  theme_get(THEME_WINDOW_BORDER));
        fill_clip(X, Y + Hn - 1, Wn, 1, x, y, w, h,
                  theme_get(THEME_WINDOW_BORDER));

        /* 标题栏带 */
        if (rects_intersect(x, y, w, h, X + 1, Y + 1, Wn - 2, TITLEBAR_H)) {
            int bx = X + Wn - 32, by = Y + (TITLEBAR_H - 18) / 2;

            fill_clip(X + 1, Y + 1, Wn - 2, TITLEBAR_H, x, y, w, h,
                      theme_get(THEME_TITLEBAR));
            if (rects_intersect(x, y, w, h, X + 12, Y + 4, 224, 32))
                font_drawtext(X + 12, Y + 4, "YuanCore Shell",
                              theme_get(THEME_TITLE_TEXT), 2);
            if (rects_intersect(x, y, w, h, bx, by, 20, 18)) {
                fb_fillrect(bx, by, 20, 18, theme_get(THEME_DANGER));
                font_drawchar(bx + 6, by + 1, 'x',
                              theme_get(THEME_TITLE_TEXT), 1);
            }
        }

        /* 状态条带(文字重画) */
        if (rects_intersect(x, y, w, h,
                            X + 1, Y + Hn - 1 - 22, Wn - 2, 22)) {
            if (rects_intersect(x, y, w, h, X + 14, Y + Hn - 22 + 3, 180, 16))
                font_drawtext(X + 14, Y + Hn - 22 + 3,
                              "type 'help' for commands",
                              theme_get(THEME_TEXT_DIM), 1);
        }
    }

    /* --- 任务栏带（右缘，最上层：最后画） --- */
    if (taskbar_visible != 0) {
        if (x + w > bar_x()) {
            int bx = (x > bar_x()) ? x : bar_x();
            int sxx, syy, sww, shh;

            /* 竖条整段重画(交集范围) */
            fb_fillrect(bx, y, x + w - bx, h, theme_get(THEME_TASKBAR));
            if (bx == bar_x())
                fb_vline(bar_x(), y, h, theme_get(THEME_TASKBAR_LINE));

            /* Start 按钮 */
            start_rect(&sxx, &syy, &sww, &shh);
            if (rects_intersect(x, y, w, h, sxx, syy, sww, shh)) {
                fb_fillrect(sxx, syy, sww, shh, theme_get(THEME_ACCENT));
                font_drawtext(sxx + (sww - 40) / 2, syy + 8, "Start",
                              theme_get(THEME_TITLE_TEXT), 1);
            }
            /* 时钟区:整区重画(它自清自绘) */
            desktop_draw_clock();
        }
    } else {
        if (x + w > W - TASKBAR_STRIP)
            draw_taskbar_strip(W, H);
    }
}

void desktop_draw(const struct multiboot_info *mbi)
{
    int w = (int)fb.width;
    int h = (int)fb.height;

    last_mbi = mbi;

    draw_wallpaper(w, h);

    /* 左侧桌面图标 */
    draw_icon(ICON_X, icon_y[0], theme_get(THEME_ICON_FILES), "Files");
    draw_icon(ICON_X, icon_y[1], theme_get(THEME_ICON_TERMINAL), "Terminal");
    draw_icon(ICON_X, icon_y[2], theme_get(THEME_ICON_SETTINGS), "Settings");
    draw_icon(ICON_X, icon_y[3], theme_get(THEME_ICON_ABOUT), "About");

    /* 终端窗口外框（内容区由 console_init 填充）；可被 × 关闭 */
    if (term_visible != 0) {
        desktop_draw_window(SHELL_WIN_X, SHELL_WIN_Y, SHELL_WIN_W,
                            SHELL_WIN_H, "YuanCore Shell");

        /* 窗口底部状态条 */
        fb_fillrect(SHELL_WIN_X + 1,
                    SHELL_WIN_Y + SHELL_WIN_H - 1 - 22,
                    SHELL_WIN_W - 2, 22, theme_get(THEME_WINDOW_BG));
        font_drawtext(SHELL_WIN_X + 14,
                      SHELL_WIN_Y + SHELL_WIN_H - 22 + 3,
                      "type 'help' for commands", theme_get(THEME_TEXT_DIM), 1);
    }

    /* 右侧竖任务栏（Win 键唤起/收起） */
    if (taskbar_visible != 0)
        draw_taskbar(w, h);
    else
        draw_taskbar_strip(w, h);
}
