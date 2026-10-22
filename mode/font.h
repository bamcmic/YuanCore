/* font.h - 图形模式下的文本绘制（基于 8x16 点阵字体，数据见 font_data.h） */
#ifndef FONT_H
#define FONT_H

/* scale 为放大倍数：1 = 8x16，2 = 16x32 */
void font_drawchar(int x, int y, char c, unsigned int color, int scale);
void font_drawtext(int x, int y, const char *s, unsigned int color, int scale);

/* 扩展绘制: 透明度混合(alpha 0..255, 需读背景像素) + 样式位 */
#define FONT_SHADOW  0x1u     /* 右下 1*scale 偏移绘制黑色衬底 */
#define FONT_BOLD    0x2u     /* 水平 +1px 双描仿粗体 */
void font_drawchar_ex(int x, int y, char c, unsigned int color, int scale,
                      unsigned int alpha, unsigned int flags);
void font_drawtext_ex(int x, int y, const char *s, unsigned int color,
                      int scale, unsigned int alpha, unsigned int flags);

/* 返回字符串按给定倍数绘制时占用的像素宽度 */
int  font_text_width(const char *s, int scale);

#endif /* FONT_H */