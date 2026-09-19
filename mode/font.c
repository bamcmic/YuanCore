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

void font_drawchar(int x, int y, char c, unsigned int color, int scale)
{
    const unsigned char *g = glyph_of(c);
    int row, col, sx, sy;

    if (scale < 1)
        scale = 1;

    for (row = 0; row < FONT_GLYPH_H; row++) {
        unsigned char bits = g[row];

        if (bits == 0)
            continue;                     /* 空行直接跳过，省一大片描点 */
        for (col = 0; col < FONT_GLYPH_W; col++) {
            if ((bits & (1 << (7 - col))) == 0)
                continue;
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++)
                    fb_putpixel(x + col * scale + sx,
                                y + row * scale + sy, color);
        }
    }
}

void font_drawtext(int x, int y, const char *s, unsigned int color, int scale)
{
    int cx = x;

    if (s == 0)
        return;
    if (scale < 1)
        scale = 1;

    while (*s) {
        font_drawchar(cx, y, *s, color, scale);
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
