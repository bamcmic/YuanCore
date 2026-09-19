/* shell/ui.c - 极简窗口 UI 工具包
 *
 * 设计取舍：
 *  - 无鼠标、无拖拽：导航 = Tab/方向键 + Enter，Esc 关闭
 *  - 模态：窗口打开期间 shell 不收字符，所有按键进窗口
 *  - 重绘策略：每次都先恢复桌面+终端再画窗口，正确性优先。
 *    全屏渐变约 78 万次描点，QEMU 下大概几十毫秒，可接受；
 *    以后加脏矩形优化。
 */
#include "ui.h"

#include "console.h"

#include "../mode/kbd.h"
#include "../mode/mouse.h"
#include "../mode/io.h"
#include "../mode/fb.h"
#include "../mode/font.h"
#include "../mode/theme.h"
#include "../mode/desktop.h"

#define UI_MAX_WINDOWS 4

#define TITLE_H DESKTOP_TITLEBAR_H
#define PAD     14

struct ui_window {
    int used;
    int x, y, w, h;
    const char *title;
    ui_draw_fn  draw;
    ui_key_fn   on_key;
    int focus;
    int focus_count;
};

static struct ui_window wins[UI_MAX_WINDOWS];
static int top = -1;        /* 最上层(最近打开)窗口 */
static int current = -1;    /* 正在绘制的窗口 */
static int dirty;           /* 有变化才重绘，按键无效果时不浪费带宽 */

/* 当前绘制窗口登记的按钮矩形（点击命中用） */
#define UI_MAX_BUTTONS 12
static int btn_x[UI_MAX_BUTTONS], btn_y[UI_MAX_BUTTONS];
static int btn_w[UI_MAX_BUTTONS], btn_h[UI_MAX_BUTTONS];
static int btn_n;

/* 光标状态 */
#define CUR_W 9
#define CUR_H 14
static int mx, my, mouse_ready;
static unsigned char cursor_saved[CUR_W * CUR_H * 4];

static void mark_dirty(void) { dirty = 1; }

static const char *cursor_bmp[CUR_H] = {
    "X........",
    "XX.......",
    "X#X......",
    "X##X.....",
    "X###X....",
    "X####X...",
    "X#####X..",
    "X######X.",
    "X#######X",
    "X####XXX.",
    "X#X#X....",
    "XX.X#X...",
    "...XX#X..",
    "....X#X.."
};

/* 把指定矩形读进缓存 */
static void region_save(int x, int y)
{
    volatile unsigned char *src = fb.addr
        + (unsigned int)y * fb.pitch + (unsigned int)x * 4;
    volatile unsigned int *s = (volatile unsigned int *)src;
    int i, n = CUR_W * CUR_H;

    if (fb.bpp != 32)
        return;
    for (i = 0; i < n; i++)
        ((unsigned int *)cursor_saved)[i] = s[i];
    (void)x; (void)y;
}

static void region_restore(int x, int y)
{
    volatile unsigned int *d = (volatile unsigned int *)
        (fb.addr + (unsigned int)y * fb.pitch + (unsigned int)x * 4);
    int i, n = CUR_W * CUR_H;

    if (fb.bpp != 32)
        return;
    for (i = 0; i < n; i++)
        d[i] = ((unsigned int *)cursor_saved)[i];
    (void)x; (void)y;
}

static void cursor_paint(void)
{
    int r, c;

    if (mouse_ready == 0)
        return;
    region_save(mx, my);

    /* 黑色投影 + 白色箭头，任何主题下都看得清 */
    for (r = 0; r < CUR_H; r++) {
        for (c = 0; c < CUR_W; c++) {
            char ch = cursor_bmp[r][c];

            if (ch == 'X')
                fb_putpixel(mx + c + 1, my + r + 1,
                            RGB(0x00, 0x00, 0x00));
        }
    }
    for (r = 0; r < CUR_H; r++) {
        for (c = 0; c < CUR_W; c++) {
            if (cursor_bmp[r][c] == 'X')
                fb_putpixel(mx + c, my + r, RGB(0xFF, 0xFF, 0xFF));
        }
    }
}

static void cursor_erase(void)
{
    if (mouse_ready == 0)
        return;
    region_restore(mx, my);
}

void ui_mouse_init(void)
{
    mx = (int)fb.width / 2;
    my = (int)fb.height / 2;
    mouse_ready = (fb.bpp == 32);
}

void ui_mouse(int dx, int dy, int btn)
{
    static int last_btn;
    int nx, ny, i;

    nx = mx + dx;
    ny = my + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx + CUR_W > (int)fb.width)  nx = (int)fb.width - CUR_W;
    if (ny + CUR_H > (int)fb.height) ny = (int)fb.height - CUR_H;

    if (nx != mx || ny != my) {
        cursor_erase();
        mx = nx;
        my = ny;
        cursor_paint();
    }

    /* 左键按下沿：命中按钮 → 聚焦并触发 */
    if ((btn & 1) != 0 && (last_btn & 1) == 0 && top >= 0 && btn_n > 0) {
        for (i = 0; i < btn_n; i++) {
            if (mx >= btn_x[i] && mx < btn_x[i] + btn_w[i]
                && my >= btn_y[i] && my < btn_y[i] + btn_h[i]) {
                wins[top].focus = i;
                mark_dirty();
                ui_draw();
                if (wins[top].on_key != 0)
                    wins[top].on_key(top, KEY_ENTER);
                if (dirty)
                    ui_draw();
                break;
            }
        }
    }
    last_btn = btn;
}

static void restore_background(void)
{
    const struct multiboot_info *mbi = desktop_mbi();

    if (mbi != 0)
        desktop_draw(mbi);
    console_redraw();
}

static int find_slot(void)
{
    int i;

    for (i = 0; i < UI_MAX_WINDOWS; i++)
        if (!wins[i].used)
            return i;
    return -1;
}

static void refresh_top(void)
{
    int i;

    top = -1;
    for (i = UI_MAX_WINDOWS - 1; i >= 0; i--) {
        if (wins[i].used) {
            top = i;
            break;
        }
    }
}

int ui_open(int x, int y, int w, int h, const char *title,
            ui_draw_fn draw, ui_key_fn on_key)
{
    int i = find_slot();

    if (i < 0)
        return -1;

    wins[i].used        = 1;
    wins[i].x           = x;
    wins[i].y           = y;
    wins[i].w           = w;
    wins[i].h           = h;
    wins[i].title       = title;
    wins[i].draw        = draw;
    wins[i].on_key      = on_key;
    wins[i].focus       = 0;
    wins[i].focus_count = 1;

    refresh_top();
    ui_draw();
    return i;
}

int ui_valid(int win)
{
    return win >= 0 && win < UI_MAX_WINDOWS && wins[win].used;
}

void ui_close(int win)
{
    if (!ui_valid(win))
        return;

    wins[win].used = 0;
    refresh_top();
    ui_draw();                  /* 恢复背景 + 画剩余窗口 */
}

void ui_close_all(void)
{
    int i;

    for (i = 0; i < UI_MAX_WINDOWS; i++)
        wins[i].used = 0;
    refresh_top();
    ui_draw();
}

int ui_active(void)
{
    return top >= 0;
}

void ui_draw(void)
{
    int i;

    restore_background();

    for (i = 0; i < UI_MAX_WINDOWS; i++) {
        if (!wins[i].used)
            continue;
        desktop_draw_window(wins[i].x, wins[i].y,
                            wins[i].w, wins[i].h, wins[i].title);
        current = i;
        btn_n = 0;                          /* 每个窗口重画时重新登记按钮 */
        wins[i].focus_count = 1;            /* 应用忘了声明时给个安全值 */
        if (wins[i].draw != 0)
            wins[i].draw(i);
        current = -1;
    }
    cursor_paint();                         /* 光标永远画在最上层 */
    dirty = 0;
}

void ui_refresh(void)
{
    mark_dirty();
    ui_draw();
}

void ui_key(int key)
{
    struct ui_window *w;

    if (top < 0)
        return;
    w = &wins[top];

    switch (key) {
    case KEY_ESC:
        ui_close(top);
        return;
    case KEY_TAB:
        if (w->focus_count > 1)
            w->focus = (w->focus + 1) % w->focus_count;
        mark_dirty();
        ui_draw();
        return;
    default:
        break;
    }

    if (w->on_key != 0)
        w->on_key(top, key);

    /* 应用内部若改了内容/焦点会 mark_dirty；纯无效按键就不重绘 */
    if (dirty)
        ui_draw();
}

void ui_client(int win, int *x, int *y, int *w, int *h)
{
    if (!ui_valid(win))
        return;

    if (x) *x = wins[win].x + PAD;
    if (y) *y = wins[win].y + TITLE_H + PAD;
    if (w) *w = wins[win].w - 2 * PAD;
    if (h) *h = wins[win].h - TITLE_H - 2 * PAD;
}

int ui_focus(void)
{
    if (current >= 0)
        return wins[current].focus;
    if (top >= 0)
        return wins[top].focus;
    return 0;
}

void ui_focus_count(int n)
{
    if (n < 1)
        n = 1;
    if (current >= 0)
        wins[current].focus_count = n;
    else if (top >= 0)
        wins[top].focus_count = n;
}

void ui_focus_next(void)
{
    if (top < 0)
        return;
    if (wins[top].focus_count > 0) {
        wins[top].focus = (wins[top].focus + 1) % wins[top].focus_count;
        mark_dirty();
    }
}

void ui_focus_prev(void)
{
    if (top < 0)
        return;
    if (wins[top].focus_count > 0) {
        wins[top].focus = (wins[top].focus + wins[top].focus_count - 1)
                          % wins[top].focus_count;
        mark_dirty();
    }
}

void ui_focus_set(int idx)
{
    if (current >= 0)
        wins[current].focus = idx;
    else if (top >= 0)
        wins[top].focus = idx;
    mark_dirty();
}

/* ---- 控件 ---- */

void ui_panel(int x, int y, int w, int h, unsigned int color)
{
    fb_fillrect(x, y, w, h, color);
}

void ui_label(int x, int y, const char *text)
{
    font_drawtext(x, y, text, theme_get(THEME_TEXT), 1);
}

/* 纯绘制版本：active 由调用方决定（用户程序经系统调用用这个） */
void ui_draw_button(int x, int y, int w, int h, const char *text, int active)
{
    unsigned int bg, fg, bd;
    int tx, ty;

    if (active) {
        bg = theme_get(THEME_ACCENT);
        fg = theme_get(THEME_TITLE_TEXT);
        bd = theme_get(THEME_TITLEBAR);
    } else {
        bg = theme_get(THEME_WINDOW_BG);
        fg = theme_get(THEME_TEXT);
        bd = theme_get(THEME_WINDOW_BORDER);
    }

    fb_fillrect(x, y, w, h, bg);
    fb_rect(x, y, w, h, bd);

    if (active)
        fb_rect(x + 2, y + 2, w - 4, h - 4, fg);

    if (text != 0) {
        tx = x + (w - font_text_width(text, 1)) / 2;
        ty = y + (h - 16) / 2 + 2;
        font_drawtext(tx, ty, text, fg, 1);
    }
}

void ui_button(int x, int y, int w, int h, const char *text, int idx)
{
    /* 登记矩形供鼠标点击命中（重复下标以后来的为准） */
    if (current >= 0 && btn_n < UI_MAX_BUTTONS) {
        btn_x[btn_n] = x;
        btn_y[btn_n] = y;
        btn_w[btn_n] = w;
        btn_h[btn_n] = h;
        btn_n++;
    }

    ui_draw_button(x, y, w, h, text, idx == ui_focus());
}

/* ---- 鼠标光标/事件（供用户程序经系统调用使用） ---- */

void ui_cursor_get(int *x, int *y)
{
    if (x) *x = mx;
    if (y) *y = my;
}

void ui_print_dec(int v)
{
    char t[12];
    int i = 0, j;

    if (v == 0) {
        console_putc('0');
        return;
    }
    if (v < 0) {
        console_putc('-');
        v = -v;
    }
    while (v > 0) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (j = 0; j < i; j++)
        console_putc(t[i - 1 - j]);
}

/* 阻塞等待一个事件：返回 1 = 键盘(*key)，2 = 鼠标(其余参数)。
 * 鼠标事件会先驱动光标。 */
int ui_wait_event(int *key, int *dx, int *dy, int *btn)
{
    for (;;) {
        int c = kbd_poll();

        if (c >= 0) {
            if (key) *key = c;
            return 1;
        }
        if (mouse_poll(dx, dy, btn)) {
            ui_mouse(*dx, *dy, *btn);
            return 2;
        }
        hlt();
    }
}
