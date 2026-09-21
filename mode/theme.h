/* theme.h - 主题/色彩设置
 *
 * 所有 UI 颜色都从这里取，desktop.c / console.c 不再硬编码颜色。
 * shell 的 settings 命令通过本接口在运行时改色并即时生效。
 */
#ifndef THEME_H
#define THEME_H

/* 颜色项索引 */
enum theme_color {
    THEME_BG_TOP = 0,
    THEME_BG_BOTTOM,
    THEME_TASKBAR,
    THEME_TASKBAR_LINE,
    THEME_TITLEBAR,
    THEME_TITLE_TEXT,
    THEME_WINDOW_BG,
    THEME_WINDOW_BORDER,
    THEME_TEXT,
    THEME_TEXT_DIM,
    THEME_ACCENT,
    THEME_DANGER,
    THEME_CONSOLE_FG,
    THEME_CONSOLE_BG,
    THEME_ICON_FILES,
    THEME_ICON_TERMINAL,
    THEME_ICON_SETTINGS,
    THEME_ICON_ABOUT,
    THEME_COLOR_COUNT
};

void         theme_init(void);                       /* 载入 default 预置 */

/* 配置持久化：读写 /settings.cfg（内存文件系统，重启即回默认）。
 * theme_init() 会自动尝试加载。 */
int          theme_save(void);
int          theme_load(void);

unsigned int theme_get(int key);
int          theme_set(int key, unsigned int rgb);   /* 成功 0，键不存在 -1 */

/* "bg_top" -> THEME_BG_TOP；找不到返回 -1 */
int          theme_key_from_name(const char *name);
const char  *theme_key_name(int key);

/* 把颜色格式化成 "RRGGBB"（写入 out，至少 7 字节），返回 out */
const char  *theme_hex(int key, char *out);

/* 应用预置主题：default / ocean / sunset / matrix / light。
 * 成功返回 0，名字未知返回 -1。 */
int          theme_preset(const char *name);
const char  *theme_first_preset(void);               /* 遍历用，返回第一个名字 */
const char  *theme_next_preset(const char *cur);     /* 没有下一个返回 0 */

/* ---- 非颜色选项（同样持久化到 /settings.cfg） ---- */

/* 壁纸样式：0=渐变(默认) 1=纯色(用 bg_top) 2=渐变+点阵纹理 */
#define THEME_WP_GRADIENT 0
#define THEME_WP_SOLID    1
#define THEME_WP_DOTS     2

int          theme_bg_style(void);
void         theme_set_bg_style(int style);          /* 越界自动取最近合法值 */

/* 任务栏时钟开关（默认开） */
int          theme_clock_show(void);
void         theme_set_clock_show(int on);

#endif /* THEME_H */
