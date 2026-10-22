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
#define ANIM_SLIDE 7        /* 入场动画每帧 y 偏移(8 帧 * 7 = 56px) */

static unsigned int lighten(unsigned int c, int k);

/* 滚动条/滚轮交互状态(ui_scroll_bar 绘制时登记, ui_mouse 使用) */
static ui_scroll *bar_sc;
static int bar_thumb_y, bar_thumb_h;
static int bar_track_x, bar_track_y, bar_track_h;
static int scroll_drag, drag_y0, drag_off0;


struct ui_window {
    int used;
    int x, y, w, h;
    const char *title;
    ui_draw_fn  draw;
    ui_key_fn   on_key;
    int focus;
    int focus_count;
    int anim;            /* 入场滑动剩余帧 */
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

/* 光标状态 —— 精度点 + 目标环（UI 2.0 光标）
 *
 *   ·  hotspot = 实心点中心，命中测试即此坐标
 *        ◯   环心在点右下 (+9,+9)，外径 8 / 内径 5
 *
 * 黑主体 + 1px 白 halo：深浅背景下都可见（与旧箭头
 * "白箭头+黑投影"同一对比度策略）。参数化画圆，无位图。
 */
#define CUR_SIZE 24
#define CUR_HOT   4            /* 点中心在画布内的坐标 */
#define RING_CX  13            /* 环心在画布内的坐标 */
#define RING_CY  13
#define SAVE_W CUR_SIZE
#define SAVE_H CUR_SIZE
static int mx, my, mouse_ready;
static int cursor_visible;      /* 没画过就绝不擦——否则首次擦除会把
                                 * 全零的黑块写上屏（就是那条"莫名黑线"） */
static int saved_w, saved_h;    /* 实际保存的宽高（屏幕边缘处会被裁剪） */
static unsigned int cursor_saved[SAVE_H][SAVE_W];

static void mark_dirty(void) { dirty = 1; }


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

/* 实心圆盘 / 圆环（光标专用,扫描包围盒逐点判断） */
static void paint_disc(int cx, int cy, int r, unsigned int c)
{
    int x, y, r2 = r * r;

    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r2)
                fb_putpixel(cx + x, cy + y, c);
}

static void paint_ring(int cx, int cy, int r_out, int r_in, unsigned int c)
{
    int x, y, ro2 = r_out * r_out, ri2 = r_in * r_in;

    for (y = -r_out; y <= r_out; y++)
        for (x = -r_out; x <= r_out; x++) {
            int d2 = x * x + y * y;

            if (d2 <= ro2 && d2 >= ri2)
                fb_putpixel(cx + x, cy + y, c);
        }
}

static void cursor_paint(void)
{
    int ox = mx - CUR_HOT, oy = my - CUR_HOT;

    if (mouse_ready == 0)
        return;
    region_save(ox, oy);

    /* 白 halo(外扩 1px) 保证深/浅背景可见，再画黑主体 */
    paint_ring(ox + RING_CX, oy + RING_CY, 9, 4,
               theme_get(THEME_CURSOR_HALO));
    paint_disc(ox + CUR_HOT, oy + CUR_HOT, 3,
               theme_get(THEME_CURSOR_HALO));

    paint_ring(ox + RING_CX, oy + RING_CY, 8, 5,
               theme_get(THEME_CURSOR_BODY));
    paint_disc(ox + CUR_HOT, oy + CUR_HOT, 2,
               theme_get(THEME_CURSOR_BODY));

    cursor_visible = 1;
}

static void cursor_erase(void)
{
    if (mouse_ready == 0 || cursor_visible == 0)
        return;
    region_restore(mx - CUR_HOT, my - CUR_HOT);
    cursor_visible = 0;
}

void ui_mouse_init(void)
{
    mx = (int)fb.width / 2;
    my = (int)fb.height / 2;
    if (mx < CUR_HOT) mx = CUR_HOT;
    if (my < CUR_HOT) my = CUR_HOT;
    if (mx + (CUR_SIZE - CUR_HOT) > (int)fb.width)
        mx = (int)fb.width - (CUR_SIZE - CUR_HOT);
    if (my + (CUR_SIZE - CUR_HOT) > (int)fb.height)
        my = (int)fb.height - (CUR_SIZE - CUR_HOT);
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
static int pan_active;              /* 空白桌面拖拽平移画布进行中 */
static int pan_lx, pan_ly;          /* 平移上一次鼠标位置 */

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

    /* 滚轮: 上报到最近绘制的滚动区(目标值, 由 ui_anim_tick 缓动) */
    {
        int w = mouse_wheel();

        if (w != 0 && bar_sc != 0) {
            ui_scroll_page(bar_sc, -w * 48);
            ui_draw();
        }
    }

    nx = mx + dx;
    ny = my + dy;
    if (nx < CUR_HOT) nx = CUR_HOT;
    if (ny < CUR_HOT) ny = CUR_HOT;
    if (nx + (CUR_SIZE - CUR_HOT) > (int)fb.width)
        nx = (int)fb.width - (CUR_SIZE - CUR_HOT);
    if (ny + (CUR_SIZE - CUR_HOT) > (int)fb.height)
        ny = (int)fb.height - (CUR_SIZE - CUR_HOT);

    if (nx != mx || ny != my) {
        int ox = mx, oy = my;

        if (drag_win < 0) {
            if (top < 0) {
                /* 合成器路径：上一个位置直接从各层重建 */
                cursor_visible = 0;
                compose_region(ox - CUR_HOT, oy - CUR_HOT, CUR_SIZE, CUR_SIZE);
            } else {
                cursor_erase();
            }
        }
        mx = nx;
        my = ny;

        /* 滚动条拖动: 按住滑块上下移动 */
        if (scroll_drag != 0 && (btn & 1) != 0 && bar_sc != 0) {
            int max = bar_sc->content_h - bar_sc->h;
            int span = bar_track_h - bar_thumb_h;

            if (span > 0 && max > 0) {
                int off = drag_off0
                        + (my - drag_y0) * max / span;

                if (off < 0) off = 0;
                if (off > max) off = max;
                bar_sc->offset = off;
                bar_sc->target = off;
                ui_draw();
            }
            last_btn = btn;
            return;
        }

        /* 按住标题栏拖动：窗口跟随(画布坐标,可跨屏),整屏重排 */
        if (drag_win >= 0 && (btn & 1) != 0) {
            int cvx, cvy, wx, wy;

            desktop_canvas_vp(&cvx, &cvy);
            wx = mx + cvx - drag_offx;
            wy = my + cvy - drag_offy;

            if (wx < 0) wx = 0;
            if (wy < 0) wy = 0;
            if (wx + wins[drag_win].w > (int)fb.width * CANVAS_COLS)
                wx = (int)fb.width * CANVAS_COLS - wins[drag_win].w;
            if (wy + wins[drag_win].h > (int)fb.height * CANVAS_ROWS)
                wy = (int)fb.height * CANVAS_ROWS - wins[drag_win].h;

            if (exp_snap != 0) {
                /* 贴边吸附: 吸到当前视口所在屏的屏缘 */
                if (wx < cvx + DRAG_SNAP)
                    wx = cvx;
                if (wy < cvy + DRAG_SNAP)
                    wy = cvy;
                if (wx + wins[drag_win].w
                    > cvx + (int)fb.width - DRAG_SNAP)
                    wx = cvx + (int)fb.width - wins[drag_win].w;
                if (wy + wins[drag_win].h
                    > cvy + (int)fb.height - DRAG_SNAP)
                    wy = cvy + (int)fb.height - wins[drag_win].h;
            }

            wins[drag_win].x = wx;
            wins[drag_win].y = wy;
            ui_draw();
            last_btn = btn;
            return;
        }

        /* 空白桌面拖拽: 1:1 平移画布(与窗口拖动互斥) */
        if (pan_active != 0 && (btn & 1) != 0) {
            if (mx != pan_lx || my != pan_ly) {
                desktop_canvas_pan(pan_lx - mx, pan_ly - my);
                pan_lx = mx;
                pan_ly = my;
                ui_draw();
                last_btn = btn;
                return;
            }
        }
        cursor_paint();
    }

    /* 左键松开：结束拖动/平移 */
    if ((btn & 1) == 0) {
        if ((last_btn & 1) != 0) {
            drag_win = -1;
            pan_active = 0;
            scroll_drag = 0;
        }
        last_btn = btn;
        return;
    }
    if ((last_btn & 1) != 0) {
        last_btn = btn;
        return;                     /* 按住中的移动：上面已处理 */
    }
    last_btn = btn;

    /* --- 有窗口：× 关闭钮 → 按钮 → 标题栏拖动(窗口为画布坐标) --- */
    if (top >= 0) {
        int cvx, cvy, wmx, wmy, inside;

        desktop_canvas_vp(&cvx, &cvy);
        wmx = mx + cvx;
        wmy = my + cvy;
        inside = wmx >= wins[top].x && wmx < wins[top].x + wins[top].w
              && wmy >= wins[top].y && wmy < wins[top].y + wins[top].h;

        /* 滚动条命中: 滑块 -> 开始拖动; 轨道 -> 翻页 */
        if (bar_sc != 0
            && mx >= bar_track_x - 3 && mx <= bar_track_x + 7
            && my >= bar_track_y && my < bar_track_y + bar_track_h) {
            if (my >= bar_thumb_y && my < bar_thumb_y + bar_thumb_h) {
                scroll_drag = 1;
                drag_y0 = my;
                drag_off0 = bar_sc->offset;
            } else {
                ui_scroll_page(bar_sc,
                               (my < bar_thumb_y) ? -bar_sc->h
                                                  : bar_sc->h);
            }
            ui_draw();
            return;
        }

        if (desktop_close_hit(wins[top].x, wins[top].y,
                              wins[top].w, wins[top].h, wmx, wmy)) {
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
            && wmx >= wins[top].x + 1 && wmx < wins[top].x + wins[top].w - 1
            && wmy >= wins[top].y + 1 && wmy < wins[top].y + 1 + TITLE_H) {
            drag_win = top;
            drag_offx = wmx - wins[top].x;
            drag_offy = wmy - wins[top].y;
        }
        if (inside)
            return;                 /* 窗口内其它区域：不穿透 */
        /* 点在窗口之外: 落穿到桌面层(雷达图/任务栏/空白平移) */
    }

    /* --- 桌面层: 提示条 / 终端× / 雷达图 / 头像 / 运行程序 / 启动图标 / Start --- */
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
    if (desktop_minimap_hit(mx, my)) {
        ui_draw();                  /* 雷达图点击跳屏 */
        return;
    }
    if (desktop_taskbar_avatar_hit(mx, my)) {
        if (launch_fn != 0)
            launch_fn(2);           /* 头像 -> Settings */
        return;
    }
    {
        int ai = desktop_taskbar_app_hit(mx, my);

        if (ai >= 0) {
            ui_raise_row(ai);       /* 点击运行中程序项: 提到最上层 */
            return;
        }
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

    /* 空白桌面按下: 开始画布拖拽平移(终端窗口上不触发) */
    if (desktop_terminal_visible() == 0
        || mx < SHELL_WIN_X || mx >= SHELL_WIN_X + SHELL_WIN_W
        || my < SHELL_WIN_Y || my >= SHELL_WIN_Y + SHELL_WIN_H) {
        pan_active = 1;
        pan_lx = mx;
        pan_ly = my;
    }
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

/* 运行中窗口枚举(任务栏列表 + 雷达图窗口点共用):
 * idx 按使用序 0..n-1, 存在返回 1; 坐标为画布坐标 */
int ui_running_list(int idx, int *x, int *y, int *w, int *h,
                    char *buf, int cap)
{
    int i, k = 0;

    for (i = 0; i < UI_MAX_WINDOWS; i++) {
        if (!wins[i].used)
            continue;
        if (k == idx) {
            int j = 0;

            if (x) *x = wins[i].x;
            if (y) *y = wins[i].y;
            if (w) *w = wins[i].w;
            if (h) *h = wins[i].h;
            if (buf != 0 && cap > 0) {
                while (wins[i].title != 0 && wins[i].title[j] != 0
                       && j < cap - 1) {
                    buf[j] = wins[i].title[j];
                    j++;
                }
                buf[j] = '\0';
            }
            return 1;
        }
        k++;
    }
    return 0;
}

/* 把第 row 个运行中窗口提到最上层(与最高槽位交换) */
void ui_raise_row(int row)
{
    int i, k = 0, t = -1, h = -1;

    for (i = 0; i < UI_MAX_WINDOWS; i++) {
        if (!wins[i].used)
            continue;
        if (k == row)
            t = i;
        h = i;
        k++;
    }
    if (t < 0 || t == h)
        return;
    {
        struct ui_window tmp = wins[t];

        wins[t] = wins[h];
        wins[h] = tmp;
    }
    refresh_top();
    ui_draw();
}

int ui_open(int x, int y, int w, int h, const char *title,
            ui_draw_fn draw, ui_key_fn on_key)
{
    int i = find_slot();

    if (i < 0)
        return -1;

    {
        int ovx, ovy;

        desktop_canvas_vp(&ovx, &ovy);  /* 新窗口开在当前视口所在屏 */
        wins[i].x = x + ovx;
        wins[i].y = y + ovy;
    }
    wins[i].used        = 1;
    wins[i].w           = w;
    wins[i].h           = h;
    wins[i].title       = title;
    wins[i].draw        = draw;
    wins[i].on_key      = on_key;
    wins[i].focus       = 0;
    wins[i].focus_count = 1;
    wins[i].anim        = 20;        /* 入场滑动 20 帧(~200ms) */

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

/* ---- 桌面 overlay 注册表(C1) ----
 * 绘制时机: 桌面恢复之后、窗口之前 -- 小部件在窗口之下。
 * 回调里可用 ui_overlay_begin()/ui_win_end() 全屏会话自绘。 */
#define UI_MAX_OVERLAYS 4
static ui_overlay_fn overlays[UI_MAX_OVERLAYS];

int ui_overlay_register(ui_overlay_fn draw)
{
    int i;

    for (i = 0; i < UI_MAX_OVERLAYS; i++) {
        if (overlays[i] == 0) {
            overlays[i] = draw;
            return i;
        }
    }
    return -1;
}

int ui_overlay_unregister(int id)
{
    if (id < 0 || id >= UI_MAX_OVERLAYS || overlays[id] == 0)
        return -1;
    overlays[id] = 0;
    return 0;
}

/* ---- 绘制会话(A1 原点状态机) ----
 * ui_win_begin(win) 后, ui_* 绘制入口以客户区左上为原点;
 * ui_overlay_begin() 后以屏幕左上为原点(全屏 overlay, 如 firstboot);
 * ui_win_end() 恢复绝对坐标。底层 fb 与 font 接口不受影响(绝对坐标)。
 * v1: 纯原点偏移, 无窗口裁剪 -- fb_putpixel 自带屏幕边界检查,
 * 窗口越屏像素自动丢弃; 窗口重叠由 ui_draw 按 z 序全量重绘解决。 */
static int org_x, org_y;
static int cur_w, cur_h;            /* 当前会话客户区尺寸 */

static int ux(int x)
{
    return x + org_x;
}

static int uy(int y)
{
    return y + org_y;
}

void ui_win_begin(int win)
{
    if (!ui_valid(win))
        return;
    {
        int cvx, cvy;

        desktop_canvas_vp(&cvx, &cvy);
        org_x = wins[win].x - cvx + PAD;
        org_y = wins[win].y - cvy + TITLE_H + PAD;
    }
    cur_w = wins[win].w - 2 * PAD;
    cur_h = wins[win].h - TITLE_H - 2 * PAD;
}

void ui_overlay_begin(void)
{
    org_x = 0;
    org_y = 0;
    cur_w = (int)fb.width;
    cur_h = (int)fb.height;
}

void ui_win_end(void)
{
    org_x = 0;
    org_y = 0;
}

void ui_client_size(int win, int *w, int *h)
{
    if (!ui_valid(win))
        return;
    if (w) *w = wins[win].w - 2 * PAD;
    if (h) *h = wins[win].h - TITLE_H - 2 * PAD;
}

/* ---- 局部坐标绘制入口(供应用在会话内使用) ---- */

void ui_drawtext(int x, int y, const char *s, unsigned int color, int scale)
{
    font_drawtext(ux(x), uy(y), s, color, scale);
}

void ui_fillrect(int x, int y, int w, int h, unsigned int color)
{
    fb_fillrect(ux(x), uy(y), w, h, color);
}

void ui_rect(int x, int y, int w, int h, unsigned int color)
{
    fb_rect(ux(x), uy(y), w, h, color);
}

void ui_hline(int x, int y, int w, unsigned int color)
{
    fb_hline(ux(x), uy(y), w, color);
}

void ui_vline(int x, int y, int h, unsigned int color)
{
    fb_vline(ux(x), uy(y), h, color);
}

void ui_putpixel(int x, int y, unsigned int color)
{
    fb_putpixel(ux(x), uy(y), color);
}

/* ---- 应用便捷层(ui_app) ---- */

void ui_app_size(int *w, int *h)
{
    if (w) *w = cur_w;
    if (h) *h = cur_h;
}

int ui_app_open(const ui_app *app, int *win_slot)
{
    if (app == 0 || win_slot == 0)
        return -1;
    if (*win_slot >= 0 && ui_valid(*win_slot)) {
        ui_refresh();                  /* 已开: 重绘一次即可 */
        return *win_slot;
    }
    if (ui_active())
        return -1;                     /* 有别的窗口, 拒绝抢占 */
    *win_slot = ui_open(app->x, app->y, app->w, app->h,
                        app->title, app->draw, app->key);
    return *win_slot;
}

void ui_app_close(int *win_slot)
{
    if (win_slot && *win_slot >= 0 && ui_valid(*win_slot))
        ui_close(*win_slot);
    if (win_slot)
        *win_slot = -1;
}

int ui_app_active(const int *win_slot)
{
    return win_slot != 0 && *win_slot >= 0 && ui_valid(*win_slot);
}

void ui_app_text(int x, int y, const char *s)
{
    ui_drawtext(x, y, s, theme_get(THEME_TEXT), 1);
}

void ui_app_text_dim(int x, int y, const char *s)
{
    ui_drawtext(x, y, s, theme_get(THEME_TEXT_DIM), 1);
}

void ui_app_text_big(int x, int y, const char *s)
{
    ui_drawtext(x, y, s, theme_get(THEME_TEXT), 2);
}

void ui_app_input(int x, int y, int w, int h,
                  const char *buf, int masked, int hot)
{
    unsigned int border = hot ? theme_get(THEME_ACCENT)
                              : theme_get(THEME_WINDOW_BORDER);
    char shown[17];
    int i, cap;

    ui_panel(x, y, w, h, theme_get(THEME_WINDOW_BG));
    ui_rect(x, y, w, h, border);
    if (buf == 0)
        return;
    cap = w / 8 - 1;
    if (cap > 16)
        cap = 16;
    for (i = 0; buf[i] && i < cap; i++)
        shown[i] = masked ? '*' : buf[i];
    shown[i] = '_';
    shown[i + 1] = '\0';
    ui_drawtext(x + 8, y + (h - 16) / 2 + 1, shown,
                theme_get(THEME_TEXT), 1);
}

void ui_draw(void)
{
    int i;

    restore_background();

    {
        int oi;

        for (oi = 0; oi < UI_MAX_OVERLAYS; oi++)
            if (overlays[oi] != 0)
                overlays[oi]();
    }

    for (i = 0; i < UI_MAX_WINDOWS; i++) {
        int cvx, cvy, wx, wy;
        int ay = 0;

        if (!wins[i].used)
            continue;
        desktop_canvas_vp(&cvx, &cvy);
        if (wins[i].anim > 0) {
            ay = wins[i].anim * ANIM_SLIDE;
            wins[i].y -= ay;            /* 临时下移: 从下方滑入 */
        }
        wx = wins[i].x - cvx;
        wy = wins[i].y - cvy;
        if (wx + wins[i].w <= 0 || wx >= (int)fb.width
            || wy + wins[i].h <= 0 || wy >= (int)fb.height) {
            wins[i].y += ay;
            continue;               /* 窗口在视口之外(画布其他屏) */
        }
        desktop_draw_window(wx, wy, wins[i].w, wins[i].h, wins[i].title);
        current = i;
        btn_n = 0;                          /* 每个窗口重画时重新登记按钮 */
        wins[i].focus_count = 1;            /* 应用忘了声明时给个安全值 */
        ui_win_begin(i);                    /* 应用内绘制以客户区为原点 */
        if (wins[i].draw != 0)
            wins[i].draw(i);
        ui_win_end();
        current = -1;
        wins[i].y += ay;                /* 恢复真实位置 */
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
    fb_fillrect(ux(x), uy(y), w, h, color);
}

void ui_label(int x, int y, const char *text)
{
    ui_drawtext(x, y, text, theme_get(THEME_TEXT), 1);
}

/* 纯绘制版本：active 由调用方决定（用户程序经系统调用用这个） */
void ui_draw_button(int x, int y, int w, int h, const char *text, int active)
{
    unsigned int bg, fg, bd;
    int tx, ty;
    int hover = 0;

    if (current >= 0) {              /* 鼠标悬停高亮(屏幕坐标) */
        int sx = ux(x), sy = uy(y);

        if (mx >= sx && mx < sx + w && my >= sy && my < sy + h)
            hover = 1;
    }

    if (active) {
        bg = theme_get(THEME_ACCENT);
        fg = theme_get(THEME_TITLE_TEXT);
        bd = theme_get(THEME_TITLEBAR);
    } else if (hover) {
        bg = lighten(theme_get(THEME_WINDOW_BG), 14);
        fg = theme_get(THEME_TEXT);
        bd = theme_get(THEME_ACCENT);
    } else {
        bg = theme_get(THEME_WINDOW_BG);
        fg = theme_get(THEME_TEXT);
        bd = theme_get(THEME_WINDOW_BORDER);
    }

    ui_fillrect(x, y, w, h, bg);
    ui_rect(x, y, w, h, bd);

    if (active)
        ui_rect(x + 2, y + 2, w - 4, h - 4, fg);

    if (text != 0) {
        tx = x + (w - font_text_width(text, 1)) / 2;
        ty = y + (h - 16) / 2 + 2;
        ui_drawtext(tx, ty, text, fg, 1);
    }
}

void ui_button(int x, int y, int w, int h, const char *text, int idx)
{
    /* 登记矩形供鼠标点击命中（重复下标以后来的为准） */
    if (current >= 0 && btn_n < UI_MAX_BUTTONS) {
        btn_x[btn_n] = ux(x);           /* 命中登记用屏幕坐标 */
        btn_y[btn_n] = uy(y);
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
        btn_x[btn_n] = ux(x);
        btn_y[btn_n] = uy(y);
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




/* ---- 动画(窗口入场滑动 + 滚动缓动 + 滚动条交互状态) ---- */


void ui_anim_tick(void)
{
    int i, changed = 0;

    for (i = 0; i < UI_MAX_WINDOWS; i++) {
        if (wins[i].used && wins[i].anim > 0) {
            wins[i].anim--;
            changed = 1;
        }
    }

    if (bar_sc != 0) {
        int d = bar_sc->target - bar_sc->offset;

        if (d != 0) {
            int step = d / 3;

            if (step == 0)
                step = (d > 0) ? 1 : -1;
            bar_sc->offset += step;
            if ((d > 0 && bar_sc->offset > bar_sc->target)
                || (d < 0 && bar_sc->offset < bar_sc->target))
                bar_sc->offset = bar_sc->target;
            changed = 1;
        }
    }

    if (changed)
        ui_draw();
}

/* 颜色提亮(各通道 +k, 饱和到 255) */
static unsigned int lighten(unsigned int c, int k)
{
    int r = (int)((c >> 16) & 0xFF) + k;
    int g = (int)((c >> 8) & 0xFF) + k;
    int b = (int)(c & 0xFF) + k;

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

/* ---- 滚动区(ui_scroll) ---- */

void ui_scroll_init(ui_scroll *sc, int x, int y, int w, int h,
                    int content_h)
{
    sc->x = x; sc->y = y; sc->w = w; sc->h = h;
    sc->content_h = content_h;
    sc->offset = 0;
    sc->target = 0;
    ui_scroll_set(sc, 0);
}

void ui_scroll_set(ui_scroll *sc, int offset)
{
    int max = sc->content_h - sc->h;

    if (max < 0)
        max = 0;
    if (offset < 0)
        offset = 0;
    if (offset > max)
        offset = max;
    sc->offset = offset;
    sc->target = offset;
}

void ui_scroll_page(ui_scroll *sc, int delta)
{
    /* 平滑滚动: 只改 target, 由 ui_anim_tick 缓动逼近 */
    int max = sc->content_h - sc->h;
    int t = sc->target + delta;

    if (max < 0)
        max = 0;
    if (t < 0)
        t = 0;
    if (t > max)
        t = max;
    sc->target = t;
}

int ui_scroll_offset(const ui_scroll *sc)
{
    return sc->offset;
}

int ui_scroll_visible(const ui_scroll *sc, int content_y, int item_h)
{
    return (content_y + item_h > sc->offset &&
            content_y < sc->offset + sc->h);
}

int ui_scroll_content_y(const ui_scroll *sc, int screen_y)
{
    return screen_y - sc->y + sc->offset;
}

void ui_scroll_bar(const ui_scroll *sc)
{
    unsigned int track = theme_get(THEME_WINDOW_BORDER);
    unsigned int thumb = theme_get(THEME_TITLEBAR);
    int bar_x = sc->x + sc->w + 2;
    int max, th, ty;

    if (sc->content_h <= sc->h) {
        if (bar_sc == sc)               /* 内容不再超高: 撤销拖动目标 */
            bar_sc = 0;
        return;
    }

    fb_vline(ux(bar_x), uy(sc->y), sc->h, track);

    max = sc->content_h - sc->h;
    th = sc->h * sc->h / sc->content_h;
    if (th < 12)
        th = 12;
    ty = sc->y + (sc->h - th) * sc->offset / max;
    fb_fillrect(ux(bar_x + 2), uy(ty), 4, th, thumb);

    /* 登记几何, 供滚轮/拖动命中(屏幕坐标) */
    bar_sc        = (ui_scroll *)sc;
    bar_track_x   = ux(bar_x);
    bar_track_y   = uy(sc->y);
    bar_track_h   = sc->h;
    bar_thumb_y   = uy(ty);
    bar_thumb_h   = th;
}

/* ---- 侧边选项卡(ui_tabs_side) ---- */

void ui_tabs_side(int x, int y, int w, int item_h,
                  const char *const *names, int count,
                  int active, int base_idx)
{
    int i;

    for (i = 0; i < count; i++) {
        ui_draw_button(x, y + i * item_h, w, item_h,
                       names[i], i == active);
        ui_note_button(x, y + i * item_h, w, item_h, base_idx + i);
    }
}

int ui_tabs_side_hit(int x, int y, int w, int item_h,
                     int count, int mx, int my)
{
    int i;

    for (i = 0; i < count; i++) {
        if (mx >= x && mx < x + w &&
            my >= y + i * item_h && my < y + (i + 1) * item_h)
            return i;
    }
    return -1;
}


/* ---- 实验项: 全局刷新率 60fps ---- */

static int rate60;

int ui_exp_rate60_on(void)
{
    return rate60;
}

void ui_exp_rate60(int on)
{
    rate60 = (on != 0);
}

