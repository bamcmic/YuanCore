/* fb.h - 线性帧缓冲图形接口
 *
 * 颜色统一用 0x00RRGGBB 表示，由驱动按当前 bpp 转成显存格式。
 */
#ifndef FB_H
#define FB_H

#include "multiboot.h"

#define RGB(r, g, b) \
    (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

/* 调色板 */
#define C_DESKTOP_TOP    RGB(0x1B, 0x3A, 0x5C)
#define C_DESKTOP_BOTTOM RGB(0x0B, 0x1A, 0x2B)
#define C_TASKBAR        RGB(0x14, 0x28, 0x3F)
#define C_WINDOW_BG      RGB(0xF2, 0xF4, 0xF7)
#define C_WINDOW_BORDER  RGB(0x8A, 0x94, 0xA6)
#define C_TITLEBAR       RGB(0x2E, 0x6C, 0xB0)
#define C_TITLEBAR_TEXT  RGB(0xFF, 0xFF, 0xFF)
#define C_TEXT           RGB(0x1F, 0x24, 0x2E)
#define C_TEXT_DIM       RGB(0x6B, 0x74, 0x82)
#define C_ACCENT         RGB(0x35, 0x9E, 0x74)
#define C_DANGER         RGB(0xC0, 0x3B, 0x3B)

typedef struct {
    volatile unsigned char *addr;
    unsigned int  pitch;
    unsigned int  width;
    unsigned int  height;
    unsigned int  bpp;
    unsigned int  type;
    int           ready;
} framebuffer_t;

extern framebuffer_t fb;

/* 从 multiboot info 初始化帧缓冲，成功返回 0 */
int  fb_init(const struct multiboot_info *mbi);
int  fb_ready(void);

void fb_putpixel(int x, int y, unsigned int color);
void fb_hline(int x, int y, int w, unsigned int color);
void fb_vline(int x, int y, int h, unsigned int color);
void fb_fillrect(int x, int y, int w, int h, unsigned int color);
void fb_rect(int x, int y, int w, int h, unsigned int color);
void fb_clear(unsigned int color);
void fb_gradient(unsigned int top, unsigned int bottom);

#endif /* FB_H */
