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

/* ---- 点击命中（鼠标点击接线的几何依据，与绘制同一份常量） ---- */

/* 桌面图标：返回 0..3（Files/Terminal/Settings/About），没点中返回 -1 */
int desktop_icon_hit(int x, int y);

/* 任务栏 Start 按钮：点中返回 1 */
int desktop_start_hit(int x, int y);

/* 窗口右上角 × 关闭钮：点中返回 1。(wx,wy,ww,wh)=窗口矩形,(px,py)=点击点 */
int desktop_close_hit(int wx, int wy, int ww, int wh, int px, int py);

/* ---- 任务栏时钟 ---- */

/* 时钟文本提供者：返回 "HH:MM:SS" 写进 buf。
 * 内核侧注册（RTC，失败退回 uptime）；离线预览注册自己的。 */
typedef void (*desktop_clock_fn)(char *buf, int max);
void desktop_set_clock_provider(desktop_clock_fn fn);

/* 是否已有时钟提供者（ui 的每秒刷新用它决定要不要重画） */
int desktop_clock_ready(void);

/* 只重画任务栏右侧时钟区（每秒一次，代价是小矩形而非整屏） */
void desktop_draw_clock(void);

/* 合成器底层：按矩形重画"桌面层"(壁纸/图标/终端框/任务栏)。
 * 控制台内容与光标不属于这层，由 ui.c 的 compose_region 编排。 */
void desktop_repaint_region(int x, int y, int w, int h);

/* ---- 任务栏（右侧竖条，Win 键唤起/收起） ---- */

/* 任务栏宽度与收起时的提示条宽度（desktop.c 定义，命中区共用） */
#define TASKBAR_W      48
#define TASKBAR_STRIP  4
#define TASKBAR_W      48
#define TASKBAR_STRIP  4

/* Win 键或点提示条：显示/收起任务栏。切换后调用方负责 ui_refresh()。 */
void desktop_taskbar_toggle(void);
int  desktop_taskbar_visible(void);

/* 收起状态下点右缘提示条也算唤起 */
int  desktop_taskbar_edge_hit(int x, int y);

/* ---- 终端窗口的显隐 ---- */

/* 关闭/重开 YuanCore Shell 窗口（重开后调用方负责 ui_refresh()）。
 * 隐藏时控制台只更新字符缓冲不上屏，重开即完整恢复。 */
void desktop_terminal_show(int on);
int  desktop_terminal_visible(void);

/* ---- 实验性选项 ---- */

/* 任务栏时钟秒显示（on=1 三行 HH/MM/SS，on=0 两行 HH/MM）。运行时生效。 */
void desktop_exp_seconds(int on);
int  desktop_exp_seconds_on(void);

#endif /* DESKTOP_H */
