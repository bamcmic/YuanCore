/* font.c - 图形模式文本绘制
 *
 * 逐像素描点绘制 8x16 字形；scale 用于把字形放大，
 * 在 1024x768 下用 scale=2 观感最好。
 */
#include "font.h"
#include "font_data.h"
#include "fb.h"

static int text_len(const char *s)
{
    int n = 0;

    if (s == 0)
        return 0;
    while (*s++)
        n++;
    return n;
}

static const unsigned char *glyph_of(char c)
{
    unsigned char u = (unsigned char)c;

    /* 越界字符统一用空格（字表第一项）代替，避免读到表外 */
    if (u < FONT_FIRST || u > FONT_LAST)
        return font8x16[0];
    return font8x16[u - FONT_FIRST];
}

/* alpha 混合写点: 0=跳过 255=直写, 其余读底混合 */
static void blend_put(int x, int y, unsigned int color, unsigned int alpha)
{
    unsigned int bg, r, g, b;

    if (alpha == 0)
        return;
    if (alpha >= 255) {
        fb_putpixel(x, y, color);
        return;
    }
    bg = fb_getpixel(x, y);
    r = ((color >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * (255 - alpha);
    g = ((color >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * (255 - alpha);
    b = (color & 0xFF) * alpha + (bg & 0xFF) * (255 - alpha);
    fb_putpixel(x, y, RGB(r / 255, g / 255, b / 255));
}

void font_drawchar_ex(int x, int y, char c, unsigned int color, int scale,
                      unsigned int alpha, unsigned int flags)
{
    const unsigned char *g = glyph_of(c);
    int row, col, sx, sy;

    if (scale < 1)
        scale = 1;

    for (row = 0; row < FONT_GLYPH_H; row++) {
        unsigned char bits = g[row];

        if (bits == 0)
            continue;
        for (col = 0; col < FONT_GLYPH_W; col++) {
            if ((bits & (1 << (7 - col))) == 0)
                continue;
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++) {
                    int px = x + col * scale + sx;
                    int py = y + row * scale + sy;

                    if (flags & FONT_SHADOW)
                        blend_put(px + scale, py + scale, 0x000000, alpha);
                    blend_put(px, py, color, alpha);
                    if (flags & FONT_BOLD)
                        blend_put(px + 1, py, color, alpha);
                }
        }
    }
}

void font_drawchar(int x, int y, char c, unsigned int color, int scale)
{
    font_drawchar_ex(x, y, c, color, scale, 255u, 0u);
}

void font_drawtext(int x, int y, const char *s, unsigned int color, int scale)
{
    font_drawtext_ex(x, y, s, color, scale, 255u, 0u);
}

void font_drawtext_ex(int x, int y, const char *s, unsigned int color,
                      int scale, unsigned int alpha, unsigned int flags)
{
    int cx = x;

    if (s == 0)
        return;
    if (scale < 1)
        scale = 1;

    while (*s) {
        font_drawchar_ex(cx, y, *s, color, scale, alpha, flags);
        cx += FONT_GLYPH_W * scale;
        s++;
    }
}

int font_text_width(const char *s, int scale)
{
    if (scale < 1)
        scale = 1;
    return text_len(s) * FONT_GLYPH_W * scale;
}
