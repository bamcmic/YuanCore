/* shell/ui.h - 极简窗口 UI 工具包
 *
 * 给其它"程序"用的：开一个窗口、在里面画控件、收按键。
 * 当前是模态的——有窗口打开时，shell 把按键全部转给最上层窗口，
 * Esc 关闭。没有鼠标，导航靠 Tab / 方向键 + Enter。
 *
 * 一个最小程序长这样：
 *
 *   static void on_draw(int win) {
 *       int x, y, w, h;
 *       ui_client(win, &x, &y, &w, &h);
 *       ui_focus_count(1);
 *       ui_button(x, y, w - 20, 30, "Click me", 0);
 *   }
 *   static void on_key(int win, int key) {
 *       if (key == KEY_ENTER) ui_close(win);
 *   }
 *   ui_open(320, 160, 360, 220, "Hello", on_draw, on_key);
 */
#ifndef SHELL_UI_H
#define SHELL_UI_H

#include "../mode/kbd.h"

typedef void (*ui_draw_fn)(int win);
typedef void (*ui_key_fn)(int win, int key);

/* 打开窗口，返回窗口 id（<0 表示开不下了）。
 * 打开后立即绘制。x/y/w/h 为屏幕绝对坐标。 */
int  ui_open(int x, int y, int w, int h, const char *title,
             ui_draw_fn draw, ui_key_fn on_key);

void ui_close(int win);          /* 关闭并恢复被盖住的桌面/终端 */
void ui_close_all(void);
/* 把第 row 个运行中窗口提到最上层(任务栏程序项点击用) */
void ui_raise_row(int row);
/* 运行中窗口枚举: idx 按使用序,存在返回 1 并回传画布坐标矩形与标题 */
int  ui_running_list(int idx, int *x, int *y, int *w, int *h,
                     char *buf, int cap);
int  ui_active(void);            /* 是否还有窗口打开 */
int  ui_valid(int win);

/* 重绘所有窗口（主题变化/内容变化后调用） */
void ui_draw(void);

/* 把一个按键交给最上层窗口处理（Esc=关闭，Tab=切焦点） */
void ui_key(int key);

/* --- 鼠标 --- */
void ui_mouse_init(void);
/* 喂一个鼠标事件：移动光标；点击命中按钮/图标/Start/× 并触发 */
void ui_mouse(int dx, int dy, int btn);

/* 桌面点击启动回调：任务栏启动图标(0=Files 1=Terminal 2=Settings 3=About)
 * 与 4=Start。由 shell 设置；ui 点击任务栏启动图标/Start 时调用。 */
void ui_set_launch(void (*fn)(int id));

/* 实验性选项（settings exp / 设置窗口 Experimental 选项卡配置） */
void ui_exp_drag(int on);        /* 窗口拖动总开关（默认开） */
void ui_exp_snap(int on);        /* 拖动贴边吸附（默认关） */
int  ui_exp_drag_on(void);
int  ui_exp_snap_on(void);
void ui_exp_refresh(int on);     /* 全局刷新 ~30fps（默认关，性能开销大） */
int  ui_exp_refresh_on(void);

/* 每秒任务栏时钟刷新（shell 主循环里调用；内部判断秒是否变化） */
void ui_clock_tick(void);

/* 恢复桌面+终端，再画上窗口。程序改了主题之类的全局观感后调用。 */
void ui_refresh(void);

/* 客户区（去掉标题栏和内边距之后的可用区域） */
void ui_client(int win, int *x, int *y, int *w, int *h);

/* 焦点管理 */
int  ui_focus(void);             /* 当前焦点索引 */
void ui_focus_set(int idx);
void ui_focus_count(int n);      /* 绘制时声明本窗口有几个可聚焦项 */
void ui_focus_next(void);        /* Tab / 下移 */
void ui_focus_prev(void);        /* 上移 */

/* 控件（坐标相对屏幕，通常用 ui_client 算出来） */
void ui_button(int x, int y, int w, int h, const char *text, int idx);
void ui_draw_button(int x, int y, int w, int h, const char *text, int active);
void ui_label(int x, int y, const char *text);
void ui_panel(int x, int y, int w, int h, unsigned int color);

/* 只登记点击命中区、不绘制（自绘控件如选项卡用）。
 * 命中后当前窗口 focus 被设为 idx,并触发一次 KEY_ENTER。 */
void ui_note_button(int x, int y, int w, int h, int idx);

/* --- 绘制会话(A1): 窗口内局部坐标 / 全屏 overlay --- */
void ui_win_begin(int win);        /* 之后 ui_* 绘制以客户区左上为原点 */
void ui_overlay_begin(void);       /* 全屏 overlay 会话(原点=屏幕左上) */
void ui_win_end(void);             /* 恢复绝对坐标 */
void ui_client_size(int win, int *w, int *h);   /* 客户区尺寸(不含原点) */
void ui_drawtext(int x, int y, const char *s, unsigned int color, int scale);
void ui_fillrect(int x, int y, int w, int h, unsigned int color);
void ui_rect(int x, int y, int w, int h, unsigned int color);
void ui_hline(int x, int y, int w, unsigned int color);
void ui_vline(int x, int y, int h, unsigned int color);
void ui_putpixel(int x, int y, unsigned int color);

/* --- 实验项: 全局刷新率 60fps(配合 exp "refresh" 使用) --- */
int  ui_exp_rate60_on(void);
void ui_exp_rate60(int on);

/* --- 应用便捷层(ui_app): 开窗/关闭/尺寸/取色样板一站式 ---
 * 典型应用只剩三步:
 *   static const ui_app my_app = {"Title", 200, 80, 400, 300,
 *                                 my_draw, my_key};
 *   static int win = -1;
 *   void my_open(void) { ui_app_open(&my_app, &win); }
 *   static void my_draw(int w) { int cw, ch; ui_app_size(&cw, &ch);
 *                                ui_app_text(8, 8, "hello"); }
 * 会话由框架自动开启: draw 内直接写窗口局部坐标。 */
typedef struct {
    const char *title;
    int x, y, w, h;
    void (*draw)(int win);     /* 会话已开, 局部坐标 */
    void (*key)(int win, int key);
} ui_app;

int  ui_app_open(const ui_app *app, int *win_slot);   /* 已开则只刷新 */
void ui_app_close(int *win_slot);
int  ui_app_active(const int *win_slot);
void ui_app_size(int *w, int *h);      /* 当前会话客户区尺寸 */
void ui_app_text(int x, int y, const char *s);          /* 正文色 scale1 */
void ui_app_text_dim(int x, int y, const char *s);      /* 次要色 */
void ui_app_text_big(int x, int y, const char *s);      /* 正文色 scale2 */
void ui_app_input(int x, int y, int w, int h,
                  const char *buf, int masked, int hot); /* 输入框 */

/* --- 桌面 overlay 注册表(C1: 非窗口桌面小部件, 画在窗口之下) --- */
typedef void (*ui_overlay_fn)(void);
int  ui_overlay_register(ui_overlay_fn draw);   /* 返回 id, -1=满 */
int  ui_overlay_unregister(int id);

/* --- 滚动区(先写滚动: 裁剪判断 + 滚动条,键盘/拖动由调用者驱动) --- */
typedef struct {
    int x, y, w, h;        /* 内容区域(屏幕坐标, 滚动条画在右侧 10px 外) */
    int content_h;         /* 内容总高 */
    int offset;            /* 当前滚动偏移(自动钳位) */
    int target;            /* 平滑滚动目标(ui_anim_tick 缓动逼近) */
} ui_scroll;

void ui_scroll_init(ui_scroll *sc, int x, int y, int w, int h,
                    int content_h);
void ui_scroll_set(ui_scroll *sc, int offset);      /* 钳位后设置 */
void ui_scroll_page(ui_scroll *sc, int delta);      /* 翻页/滚轮步进 */
int  ui_scroll_offset(const ui_scroll *sc);
/* 内容坐标 y(含 item 高) 是否落在可视区(部分可见也算) */
int  ui_scroll_visible(const ui_scroll *sc, int content_y, int item_h);
/* 屏幕y -> 内容y (调用者再做命中) */
int  ui_scroll_content_y(const ui_scroll *sc, int screen_y);
void ui_scroll_bar(const ui_scroll *sc);            /* 右侧滚动条(可拖动) */

/* 每帧推进动画(窗口入场滑动/滚动缓动);有变化时内部重绘。
 * 由 shell 主循环每轮调用,空闲时靠 timer 中断唤醒。 */
void ui_anim_tick(void);

/* --- 侧边选项卡(垂直 tab 条; 命中/焦点复用 note_button 机制) --- */
void ui_tabs_side(int x, int y, int w, int item_h,
                  const char *const *names, int count,
                  int active, int base_idx);
/* 鼠标命中: 返回项索引(-1 无)。mx/my 为屏幕坐标。 */
int  ui_tabs_side_hit(int x, int y, int w, int item_h,
                      int count, int mx, int my);

/* --- 供用户程序系统调用使用的服务 --- */
void ui_cursor_get(int *x, int *y);
void ui_print_dec(int v);
int  ui_wait_event(int *key, int *dx, int *dy, int *btn);

#endif /* SHELL_UI_H */
