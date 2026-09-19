/* desktop.h - 图形桌面绘制
 *
 * 与内核入口解耦：内核和离线预览程序都可以调用 desktop_draw()。
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "multiboot.h"

/* shell 终端窗口的固定位置，console_init 用同一组常量对齐 */
#define SHELL_WIN_X 180
#define SHELL_WIN_Y 90
#define SHELL_WIN_W 664
#define SHELL_WIN_H 560
#define SHELL_PAD   14

/* 需要 framebuffer 已由 fb_init() 初始化完成 */
void desktop_draw(const struct multiboot_info *mbi);

/* 最近一次 desktop_draw 收到的 mbi，供 shell 重绘用 */
const struct multiboot_info *desktop_mbi(void);

/* 画一个窗口外框（阴影+边框+标题栏+关闭钮），UI 工具包用它 */
#define DESKTOP_TITLEBAR_H 36
void desktop_draw_window(int x, int y, int w, int h, const char *title);

#endif /* DESKTOP_H */
