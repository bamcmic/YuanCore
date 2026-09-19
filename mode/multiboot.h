/* multiboot.h - Multiboot 1 规范的引导信息结构体
 *
 * 偏移必须与规范严格一致，尤其是 framebuffer_* 字段（0x58 起），
 * 下面用 _Static_assert 在编译期把关键偏移钉死，改坏了会直接报错。
 */
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

/* multiboot_info.flags 位 */
#define MBI_FLAG_MEM        0x00000001
#define MBI_FLAG_MMAP       0x00000040
#define MBI_FLAG_VBE        0x00000400
#define MBI_FLAG_FRAMEBUFFER 0x00000800

/* framebuffer_type 取值 */
#define MB_FB_TYPE_INDEXED  0
#define MB_FB_TYPE_RGB      1
#define MB_FB_TYPE_TEXT     2

struct multiboot_info {
    unsigned int    flags;

    unsigned int    mem_lower;
    unsigned int    mem_upper;

    unsigned int    boot_device;
    unsigned int    cmdline;
    unsigned int    mods_count;
    unsigned int    mods_addr;

    unsigned int    syms[4];

    unsigned int    mmap_length;
    unsigned int    mmap_addr;

    unsigned int    drives_length;
    unsigned int    drives_addr;

    unsigned int    config_table;
    unsigned int    boot_loader_name;
    unsigned int    apm_table;

    unsigned int    vbe_control_info;
    unsigned int    vbe_mode_info;
    unsigned short  vbe_mode;
    unsigned short  vbe_interface_seg;
    unsigned short  vbe_interface_off;
    unsigned short  vbe_interface_len;

    unsigned long long framebuffer_addr;    /* 0x58 */
    unsigned int    framebuffer_pitch;      /* 0x60 */
    unsigned int    framebuffer_width;      /* 0x64 */
    unsigned int    framebuffer_height;     /* 0x68 */
    unsigned char   framebuffer_bpp;        /* 0x6C */
    unsigned char   framebuffer_type;       /* 0x6D */
    unsigned char   color_info[6];          /* 0x6E */
};

_Static_assert(__builtin_offsetof(struct multiboot_info, framebuffer_addr) == 0x58,
               "framebuffer_addr must be at 0x58");
_Static_assert(__builtin_offsetof(struct multiboot_info, framebuffer_pitch) == 0x60,
               "framebuffer_pitch must be at 0x60");
_Static_assert(__builtin_offsetof(struct multiboot_info, framebuffer_bpp) == 0x6C,
               "framebuffer_bpp must be at 0x6C");
_Static_assert(__builtin_offsetof(struct multiboot_info, framebuffer_type) == 0x6D,
               "framebuffer_type must be at 0x6D");

#endif /* MULTIBOOT_H */
