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
int  ui_active(void);            /* 是否还有窗口打开 */
int  ui_valid(int win);

/* 重绘所有窗口（主题变化/内容变化后调用） */
void ui_draw(void);

/* 把一个按键交给最上层窗口处理（Esc=关闭，Tab=切焦点） */
void ui_key(int key);

/* --- 鼠标 --- */
void ui_mouse_init(void);
/* 喂一个鼠标事件：移动光标，左键点击命中按钮则聚焦并触发 */
void ui_mouse(int dx, int dy, int btn);

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

/* --- 供用户程序系统调用使用的服务 --- */
void ui_cursor_get(int *x, int *y);
void ui_print_dec(int v);
int  ui_wait_event(int *key, int *dx, int *dy, int *btn);

#endif /* SHELL_UI_H */
