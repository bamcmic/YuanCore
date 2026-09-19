/* fb.c - 线性帧缓冲图形实现
 *
 * GRUB 依据 Multiboot 头部的 video 请求设置好图形模式，并把
 * framebuffer_addr/pitch/width/height/bpp 填进 multiboot info。
 * 我们不做分页，物理地址可直接访问。
 */
#include "fb.h"
#include <stdint.h>

framebuffer_t fb;

int fb_ready(void)
{
    return fb.ready;
}

static int fb_supported(void)
{
    /* 只支持 RGB 直色模式；索引色需要操作调色板，暂不支持 */
    if (fb.type != MB_FB_TYPE_RGB)
        return 0;
    return fb.bpp == 32 || fb.bpp == 24 || fb.bpp == 16;
}

int fb_init(const struct multiboot_info *mbi)
{
    fb.ready = 0;
    fb.addr  = 0;
    fb.pitch = fb.width = fb.height = fb.bpp = 0;
    fb.type  = 0;

    if (mbi == 0)
        return -1;

    /* bit 11: framebuffer 字段有效 */
    if ((mbi->flags & MBI_FLAG_FRAMEBUFFER) == 0)
        return -2;

    /* uintptr_t 随指针宽度变化：i386 内核里是 32 位，64 位宿主预览时是 64 位。
     * 不能写死 unsigned int/unsigned long —— Windows x64 上 long 仍是 32 位，
     * 会把帧缓冲地址截断。 */
    fb.addr   = (volatile unsigned char *)(uintptr_t)mbi->framebuffer_addr;
    fb.pitch  = mbi->framebuffer_pitch;
    fb.width  = mbi->framebuffer_width;
    fb.height = mbi->framebuffer_height;
    fb.bpp    = mbi->framebuffer_bpp;
    fb.type   = mbi->framebuffer_type;

    if (fb.addr == 0 || fb.width == 0 || fb.height == 0 || fb.pitch == 0)
        return -3;

    if (!fb_supported())
        return -4;

    fb.ready = 1;
    return 0;
}

void fb_putpixel(int x, int y, unsigned int color)
{
    volatile unsigned char *p;
    unsigned int off;

    if (!fb.ready || x < 0 || y < 0)
        return;
    if ((unsigned int)x >= fb.width || (unsigned int)y >= fb.height)
        return;

    off = (unsigned int)y * fb.pitch + (unsigned int)x * (fb.bpp / 8);
    p = fb.addr + off;

    if (fb.bpp == 32) {
        /* VBE 32bpp: R@16 G@8 B@0，小端写入 0x00RRGGBB 正好对上 */
        *(volatile unsigned int *)p = color;
    } else if (fb.bpp == 24) {
        p[0] = (unsigned char)(color & 0xFF);
        p[1] = (unsigned char)((color >> 8) & 0xFF);
        p[2] = (unsigned char)((color >> 16) & 0xFF);
    } else if (fb.bpp == 16) {
        /* RGB565 */
        unsigned short c = (unsigned short)(((color >> 8) & 0xF800)
                                          | ((color >> 5) & 0x07E0)
                                          |  ((color >> 3) & 0x001F));
        *(volatile unsigned short *)p = c;
    }
}

void fb_hline(int x, int y, int w, unsigned int color)
{
    int i;

    if (y < 0 || (unsigned int)y >= fb.height)
        return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > (int)fb.width)
        w = (int)fb.width - x;

    /* 32bpp 快路径：直接写显存，省掉每像素一次函数调用。
     * 渐变+清屏这种大面积操作，这是最大的热点。 */
    if (fb.ready && fb.bpp == 32 && w > 0) {
        volatile unsigned int *p = (volatile unsigned int *)
            (fb.addr + (unsigned int)y * fb.pitch + (unsigned int)x * 4);
        for (i = 0; i < w; i++)
            p[i] = color;
        return;
    }

    for (i = 0; i < w; i++)
        fb_putpixel(x + i, y, color);
}

void fb_vline(int x, int y, int h, unsigned int color)
{
    int i;

    if (x < 0 || (unsigned int)x >= fb.width)
        return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > (int)fb.height)
        h = (int)fb.height - y;
    for (i = 0; i < h; i++)
        fb_putpixel(x, y + i, color);
}

void fb_fillrect(int x, int y, int w, int h, unsigned int color)
{
    int iy;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb.width)
        w = (int)fb.width - x;
    if (y + h > (int)fb.height)
        h = (int)fb.height - y;

    /* 32bpp 快路径：整行整行直写，不再走 fb_putpixel */
    if (fb.ready && fb.bpp == 32 && w > 0 && h > 0) {
        for (iy = 0; iy < h; iy++) {
            volatile unsigned int *p = (volatile unsigned int *)
                (fb.addr + (unsigned int)(y + iy) * fb.pitch
                         + (unsigned int)x * 4);
            int ix;

            for (ix = 0; ix < w; ix++)
                p[ix] = color;
        }
        return;
    }

    for (iy = 0; iy < h; iy++)
        fb_hline(x, y + iy, w, color);
}

void fb_rect(int x, int y, int w, int h, unsigned int color)
{
    fb_hline(x, y, w, color);
    fb_hline(x, y + h - 1, w, color);
    fb_vline(x, y, h, color);
    fb_vline(x + w - 1, y, h, color);
}

void fb_clear(unsigned int color)
{
    fb_fillrect(0, 0, (int)fb.width, (int)fb.height, color);
}

void fb_gradient(unsigned int top, unsigned int bottom)
{
    unsigned int y;
    unsigned int h = fb.height ? fb.height : 1;

    for (y = 0; y < fb.height; y++) {
        /* 注意：两次独立整除会让结果抖动 ±1（渐变出现非单调条纹），
         * 必须先加权再统一取整。 */
        unsigned int t = y * 256 / h;
        unsigned int r = ((((top >> 16) & 0xFF) * (255 - t)
                         + ((bottom >> 16) & 0xFF) * t) / 256);
        unsigned int g = ((((top >> 8) & 0xFF) * (255 - t)
                         + ((bottom >> 8) & 0xFF) * t) / 256);
        unsigned int b = (((top & 0xFF) * (255 - t)
                         + (bottom & 0xFF) * t) / 256);
        fb_hline(0, (int)y, (int)fb.width, RGB(r, g, b));
    }
}
