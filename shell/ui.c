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
#include "../mode/timer.h"
#include "../mode/tss.h"
#include "../mode/idt.h"
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
/* 箭头的黑色投影偏移 +1，画在 CUR_W x CUR_H 之外 ——
 * 保存块必须比箭头大一圈，否则每次移动都在右/下留 1px 黑线（拖影） */
#define SAVE_W (CUR_W + 1)
#define SAVE_H (CUR_H + 1)
static int mx, my, mouse_ready;
static int cursor_visible;      /* 没画过就绝不擦——否则首次擦除会把
                                 * 全零的黑块写上屏（就是那条"莫名黑线"） */
static int saved_w, saved_h;    /* 实际保存的宽高（屏幕边缘处会被裁剪） */
static unsigned char cursor_saved[SAVE_H][SAVE_W * 4];

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

/* 把光标矩形（含投影的 SAVE_W x SAVE_H）整块读进缓存，逐行按 pitch 走。
 * 屏幕边缘处裁剪实际保存范围，restore 用同一组范围写回。 */
static void region_save(int x, int y)
{
    int r, c;

    saved_w = 0;
    saved_h = 0;
    if (fb.bpp != 32)
        return;

    saved_w = (int)fb.width - x;
    if (saved_w > SAVE_W) saved_w = SAVE_W;
    saved_h = (int)fb.height - y;
    if (saved_h > SAVE_H) saved_h = SAVE_H;
    if (saved_w <= 0 || saved_h <= 0) {
        saved_w = saved_h = 0;
        return;
    }

    for (r = 0; r < saved_h; r++) {
        volatile unsigned int *s = (volatile unsigned int *)
            (fb.addr + (unsigned int)(y + r) * fb.pitch + (unsigned int)x * 4);

        for (c = 0; c < saved_w; c++)
            ((unsigned int *)cursor_saved)[r * SAVE_W + c] = s[c];
    }
}

static void region_restore(int x, int y)
{
    int r, c;

    if (fb.bpp != 32 || saved_w <= 0 || saved_h <= 0)
        return;
    for (r = 0; r < saved_h; r++) {
        volatile unsigned int *d = (volatile unsigned int *)
            (fb.addr + (unsigned int)(y + r) * fb.pitch + (unsigned int)x * 4);

        for (c = 0; c < saved_w; c++)
            d[c] = ((unsigned int *)cursor_saved)[r * SAVE_W + c];
    }
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
    cursor_visible = 1;
}

static void cursor_erase(void)
{
    if (mouse_ready == 0 || cursor_visible == 0)
        return;
    region_restore(mx, my);
    cursor_visible = 0;
}

void ui_mouse_init(void)
{
    mx = (int)fb.width / 2;
    my = (int)fb.height / 2;
    mouse_ready = (fb.bpp == 32);
    cursor_visible = 0;
}

/* ---- 桌面点击启动 ---- */

static void (*launch_fn)(int id);

void ui_set_launch(void (*fn)(int id))
{
    launch_fn = fn;
}

/* ---- 窗口拖动（按住标题栏移动） ---- */

static int drag_win = -1;
static int drag_offx, drag_offy;

/* 实验性开关：拖动总开关 / 贴边吸附（settings exp 配置） */
static int exp_drag = 1;
static int exp_snap;

#define DRAG_SNAP 16

void ui_exp_drag(int on) { exp_drag = (on != 0); }
void ui_exp_snap(int on) { exp_snap = (on != 0); }
int  ui_exp_drag_on(void) { return exp_drag; }
int  ui_exp_snap_on(void) { return exp_snap; }

/* 实验性:全局刷新。开启后 shell 主循环按 ~30fps 调 ui_draw() 整屏重绘。
 * QEMU(TCG) 下整屏重绘约几十毫秒,30fps 会明显拖慢交互 —— 诊断/演示用。 */
static int exp_refresh;

void ui_exp_refresh(int on) { exp_refresh = (on != 0); }
int  ui_exp_refresh_on(void) { return exp_refresh; }

/* ---- 每秒时钟 ---- */

void ui_clock_tick(void)
{
    static unsigned int last_sec = 0xFFFFFFFF;
    unsigned int sec = timer_ticks() / 100;   /* PIT 100Hz */
    static int canary_warned;

    if (tss_canary_ok() == 0 && canary_warned == 0) {
        canary_warned = 1;
        console_puts("\n[!!!] KERNEL IRQ STACK OVERFLOWED - about to die\n");
    }

    /* IDT 完整性巡检：被踩立刻喊出（配合只读页，写它的代码会在自己
     * 的 EIP 处页错误——两条线索一起锁定肇事函数） */
    {
        static int idt_warned;

        if (idt_verify() != 0 && idt_warned == 0) {
            idt_warned = 1;
            console_puts("\n[!!!] IDT CORRUPTED - gate was overwritten\n");
        }
    }

    if (sec == last_sec)
        return;
    last_sec = sec;

    if (theme_clock_show() == 0 || desktop_clock_ready() == 0)
        return;

    /* 时钟区可能被光标压着，先擦再画再补 */
    cursor_erase();
    desktop_draw_clock();
    cursor_paint();
}

/* ---- 屏幕合成器 ----
 *
 * 分层:桌面(desktop.c) → 控制台(console.c) → 模态窗口 → 光标。
 * compose_region 把一个矩形从各层"按 z 序重新合成"出来。
 * 无窗口时擦光标 = 纯合成,零备份、零残影;有窗口时窗口静止,
 * 仍走备份恢复(窗口小,备份准确且便宜)。
 */
static void compose_region(int x, int y, int w, int h)
{
    desktop_repaint_region(x, y, w, h);
    console_repaint_region(x, y, w, h);
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
        int ox = mx, oy = my;

        if (drag_win < 0) {
            if (top < 0) {
                /* 合成器路径：上一个位置直接从各层重建 */
                cursor_visible = 0;
                compose_region(ox, oy, SAVE_W, SAVE_H);
            } else {
                cursor_erase();
            }
        }
        mx = nx;
        my = ny;

        /* 按住标题栏拖动：窗口跟随，整屏重排（背景+窗口+光标） */
        if (drag_win >= 0 && (btn & 1) != 0) {
            int wx = mx - drag_offx, wy = my - drag_offy;

            if (wx < 0) wx = 0;
            if (wy < 0) wy = 0;
            if (wx + wins[drag_win].w > (int)fb.width)
                wx = (int)fb.width - wins[drag_win].w;
            if (wy + wins[drag_win].h > (int)fb.height)
                wy = (int)fb.height - wins[drag_win].h;

            if (exp_snap != 0) {
                /* 贴边吸附：距任一屏缘 DRAG_SNAP 内则吸齐 */
                if (wx < DRAG_SNAP)                                wx = 0;
                if (wy < DRAG_SNAP)                                wy = 0;
                if (wx + wins[drag_win].w > (int)fb.width - DRAG_SNAP)
                    wx = (int)fb.width - wins[drag_win].w;
                if (wy + wins[drag_win].h > (int)fb.height - DRAG_SNAP)
                    wy = (int)fb.height - wins[drag_win].h;
            }

            wins[drag_win].x = wx;
            wins[drag_win].y = wy;
            ui_draw();
            last_btn = btn;
            return;
        }
        cursor_paint();
    }

    /* 左键松开：结束拖动 */
    if ((btn & 1) == 0) {
        if ((last_btn & 1) != 0)
            drag_win = -1;
        last_btn = btn;
        return;
    }
    if ((last_btn & 1) != 0) {
        last_btn = btn;
        return;                     /* 按住中的移动：上面已处理 */
    }
    last_btn = btn;

    /* --- 有窗口：× 关闭钮 → 按钮 → 标题栏拖动 --- */
    if (top >= 0) {
        if (desktop_close_hit(wins[top].x, wins[top].y,
                              wins[top].w, wins[top].h, mx, my)) {
            ui_close(top);
            return;
        }
        if (btn_n > 0) {
            for (i = 0; i < btn_n; i++) {
                if (mx >= btn_x[i] && mx < btn_x[i] + btn_w[i]
                    && my >= btn_y[i] && my < btn_y[i] + btn_h[i]) {
                    wins[top].focus = i;
                    ui_draw();
                    if (wins[top].on_key != 0)
                        wins[top].on_key(top, KEY_ENTER);
                    if (dirty)
                        ui_draw();
                    return;
                }
            }
        }
        /* 标题栏（避开 × 和边框）：开始拖动（实验开关 exp_drag 可禁用） */
        if (exp_drag != 0
            && mx >= wins[top].x + 1 && mx < wins[top].x + wins[top].w - 1
            && my >= wins[top].y + 1 && my < wins[top].y + 1 + TITLE_H) {
            drag_win = top;
            drag_offx = mx - wins[top].x;
            drag_offy = my - wins[top].y;
        }
        return;                     /* 窗口内其它区域：不穿透 */
    }

    /* --- 无窗口：任务栏提示条 / 终端 × / 图标 / Start --- */
    if (desktop_taskbar_edge_hit(mx, my)) {
        desktop_taskbar_toggle();
        ui_refresh();
        return;
    }
    if (desktop_terminal_visible() != 0
        && desktop_close_hit(SHELL_WIN_X, SHELL_WIN_Y,
                             SHELL_WIN_W, SHELL_WIN_H, mx, my)) {
        desktop_terminal_show(0);   /* 关闭 YuanCore Shell 窗口 */
        ui_refresh();
        return;
    }

    if (launch_fn == 0)
        return;

    i = desktop_icon_hit(mx, my);
    if (i >= 0) {
        launch_fn(i);
        return;
    }
    if (desktop_start_hit(mx, my))
        launch_fn(4);
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

/* 自绘控件(选项卡等)只登记命中区:命中后 focus=idx 并派发 KEY_ENTER */
void ui_note_button(int x, int y, int w, int h, int idx)
{
    (void)idx;                  /* 命中派发用数组序号,登记顺序须与 idx 一致 */
    if (current >= 0 && btn_n < UI_MAX_BUTTONS) {
        btn_x[btn_n] = x;
        btn_y[btn_n] = y;
        btn_w[btn_n] = w;
        btn_h[btn_n] = h;
        btn_n++;
    }
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
