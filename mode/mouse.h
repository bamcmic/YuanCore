/* mouse.h - PS/2 鼠标驱动 */
#ifndef MOUSE_H
#define MOUSE_H

void mouse_init(void);

/* 非阻塞取一个事件；有则返回 1 并填 dx/dy/btn（bit0 左键 bit1 右键），
 * 没有返回 0。 */
int mouse_poll(int *dx, int *dy, int *btn);

#endif /* MOUSE_H */
