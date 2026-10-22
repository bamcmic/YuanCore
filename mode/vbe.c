/* vbe.c - Bochs dispi 运行时分辨率切换(QEMU std VGA)
 * 0x01CE/0x01CF 索引-数据口; bpp 5 = 32bpp; LFB|NOCLEARS 保留内容。
 * 帧缓冲物理基址不变(恒等映射已覆盖), 只更新 fb 几何信息。
 */
#include "vbe.h"

#include "fb.h"
#include "io.h"

#define DISPI_INDEX 0x01CE
#define DISPI_DATA  0x01CF

#define IDX_XRES    0x01
#define IDX_YRES    0x02
#define IDX_BPP     0x03
#define IDX_ENABLE  0x04
#define IDX_VIRT_W  0x06
#define IDX_VIRT_H  0x07

#define EN_ENABLED  0x01
#define EN_LFB      0x40
#define EN_NOCLEAR  0x80

static inline void outw(unsigned short port, unsigned short val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static void dispi_set(unsigned idx, unsigned val)
{
    outw(DISPI_INDEX, idx);
    outw(DISPI_DATA, val);
}

static unsigned int dispi_get(unsigned int idx)
{
    outw(DISPI_INDEX, (unsigned short)idx);
    return (unsigned int)inw(DISPI_DATA);
}

int vbe_set_mode(int w, int h)
{
    unsigned int old_w, old_h;

    if (!fb_ready() || w <= 0 || h <= 0)
        return -1;

    old_w = fb.width;
    old_h = fb.height;

    dispi_set(IDX_ENABLE, 0);
    dispi_set(IDX_XRES, (unsigned)w);
    dispi_set(IDX_YRES, (unsigned)h);
    dispi_set(IDX_BPP, 5);              /* 32bpp */
    dispi_set(IDX_VIRT_W, (unsigned)w);
    dispi_set(IDX_VIRT_H, (unsigned)h);
    dispi_set(IDX_ENABLE, EN_ENABLED | EN_LFB | EN_NOCLEAR);

    /* 读回验证:真机没有 Bochs dispi,写入被忽略 —— 此时绝不能
     * 把 fb 几何改掉,否则绘制与真实显存模式错位(花屏/越界)。*/
    if (dispi_get(IDX_XRES) != (unsigned int)w
        || dispi_get(IDX_YRES) != (unsigned int)h) {
        dispi_set(IDX_XRES, old_w);
        dispi_set(IDX_YRES, old_h);
        return -1;
    }

    fb.width  = (unsigned int)w;
    fb.height = (unsigned int)h;
    fb.pitch  = (unsigned int)w * 4u;
    return 0;
}

/* 启动时争取高分辨率:从 1080p 起逐级回退。
 * QEMU(Bochs dispi)下真实生效并读回验证;真机端口无效,直接返回 -1,
 * 分辨率仍由 GRUB 的 multiboot2 fb tag 决定。返回 0 = 已切到
 * 列表中某个模式(或当前已是更高模式)。*/
int vbe_try_hires(void)
{
    /* 取 QEMU std VGA 的 VBE 模式表里真实存在的模式:1080p 不在表内
     * (强设只改寄存器,显示管线不认 → 黑屏),1200p 在表内且显存够
     * (1920*1200*4 = 9.2MB < 16MB)。真机 dispi 无效,列表不生效。 */
    static const int modes[][2] = {
        { 1920, 1200 }, { 1600, 1200 }, { 1280, 1024 },
        { 1280, 800 },  { 1024, 768 }
    };
    unsigned int i;

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (modes[i][0] == (int)fb.width
            && modes[i][1] == (int)fb.height)
            return 0;               /* 已是目标模式 */
        if (vbe_set_mode(modes[i][0], modes[i][1]) == 0)
            return 0;
    }
    return -1;
}
