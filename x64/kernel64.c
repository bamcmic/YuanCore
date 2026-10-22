/* x64/kernel64.c - x86-64 内核入口
 *
 * kmain64(magic, mbi_addr) 由 boot32.asm 的长模式桩调用
 * (rdi = MBI 物理地址, rsi = multiboot2 魔数)。
 *
 * 职责:解析 multiboot2 信息 tag,合成本项目既有的 multiboot1
 * 风格信息结构,然后走与 i386 版完全相同的初始化序列
 * (kernel.c 的 kernel_main_common)—— fb/desktop/console/shell
 * 等全部模块零改动复用。
 */
#include <stdint.h>

#include "multiboot2.h"
#include "registers64.h"
#include "idt64.h"

#include "../mode/fb.h"
#include "../mode/font.h"
#include "../mode/print.h"
#include "../mode/multiboot.h"
#include "../mode/io.h"

void kernel_main_common(const struct multiboot_info *mbi);

/* ---- COM1 引导信标:黑屏/死循环时定位卡点(QEMU 串口不校验波特率) ---- */
static void ser_putc(char c)
{
    outb(0x3F8, (unsigned char)c);
}

static void ser_puts(const char *s)
{
    while (*s)
        ser_putc(*s++);
}

/* kernel.c 的 kernel_main_common(x64 分支)调用的信标入口 */
void ser_puts64(const char *s)
{
    ser_puts(s);
}

/* 16 位 hex(64 位地址用),panic 页专用 */
void fmt_hex64(uint64_t v, char *out)
{
    static const char *d = "0123456789ABCDEF";
    int i;

    out[16] = '\0';
    for (i = 15; i >= 0; i--) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
}

/* ---- multiboot2 tag 遍历 ---- */

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

/* 把 mb2 信息合成 multiboot1 风格结构(静态,内核生命期内有效) */
static struct multiboot_info synth_mbi;
static unsigned char synth_mmap[4096];

/* ---- boot modules(MB2_TAG_MODULES): YCP 应用包由 GRUB 载入内存 ---- */
#define MB2_MAX_MODULES 8
static unsigned long mod_start_a[MB2_MAX_MODULES];
static unsigned long mod_end_a[MB2_MAX_MODULES];
static char mod_cmd[MB2_MAX_MODULES][64];
static int mod_cnt;

int mb2_module_count(void)
{
    return mod_cnt;
}

unsigned long mb2_module_start(int i)
{
    return (i >= 0 && i < mod_cnt) ? mod_start_a[i] : 0;
}

unsigned long mb2_module_end(int i)
{
    return (i >= 0 && i < mod_cnt) ? mod_end_a[i] : 0;
}

const char *mb2_module_cmd(int i)
{
    return (i >= 0 && i < mod_cnt) ? mod_cmd[i] : 0;
}

static const struct multiboot_info *parse_mb2(uint64_t mbi_addr)
{
    struct mb2_tag *tag = (struct mb2_tag *)(unsigned long)(mbi_addr + 8);
    unsigned int mmap_count = 0;
    unsigned char *mm = synth_mmap;
    unsigned char *mm_end = synth_mmap + sizeof synth_mmap - 32;

    for (;;) {
        if (tag->type == MB2_TAG_END)
            break;

        switch (tag->type) {
        case MB2_TAG_MODULES: {
            struct {
                uint32_t type;
                uint32_t size;
                uint32_t mod_start;
                uint32_t mod_end;
                uint32_t cmdline;
                uint32_t reserved;
            } *t = (void *)tag;
            const char *cmd = (const char *)(unsigned long)t->cmdline;

            if (mod_cnt < MB2_MAX_MODULES) {
                int n = mod_cnt, i = 0;

                mod_start_a[n] = t->mod_start;
                mod_end_a[n] = t->mod_end;
                if (cmd != 0)
                    while (cmd[i] && i < (int)sizeof mod_cmd[0] - 1) {
                        mod_cmd[n][i] = cmd[i];
                        i++;
                    }
                mod_cmd[n][i] = '\0';
                mod_cnt = n + 1;
            }
            break;
        }
        case MB2_TAG_MEMINFO: {
            struct {
                uint32_t type;
                uint32_t size;
                uint32_t mem_lower;
                uint32_t mem_upper;
            } *t = (void *)tag;

            synth_mbi.flags |= MBI_FLAG_MEM;
            synth_mbi.mem_lower = t->mem_lower;
            synth_mbi.mem_upper = t->mem_upper;
            break;
        }

        case MB2_TAG_MMAP: {
            struct {
                uint32_t type;
                uint32_t size;
                uint32_t entry_size;
                uint32_t entry_version;
            } *t = (void *)tag;
            unsigned char *p = (unsigned char *)tag + sizeof *t;
            unsigned char *end = (unsigned char *)tag + t->size;

            while (p + sizeof(struct mb2_mmap_entry) <= end
                   && mm + MB1_MMAP_ENTRY_SIZE <= mm_end) {
                struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)p;
                /* 合成 multiboot1 格式:size 在开头,值为 20(不含自身 4 字节,
                 * 条目总长 24) —— pmm.c 按 MB1 规范 size+4 步进。 */
                *(unsigned int *)mm = MB1_MMAP_SIZE_FIELD;
                *(uint64_t *)(void *)(mm + 4)  = e->base;
                *(uint64_t *)(void *)(mm + 12) = e->length;
                *(unsigned int *)(void *)(mm + 20) = e->type;

                if (e->type == MB1_MMAP_TYPE_AVAILABLE)
                    mmap_count++;
                mm += MB1_MMAP_ENTRY_SIZE;
                p += t->entry_size;
            }

            synth_mbi.flags |= MBI_FLAG_MMAP;
            synth_mbi.mmap_addr =
                (unsigned int)(unsigned long)synth_mmap;
            synth_mbi.mmap_length =
                (unsigned int)((unsigned long)mm - (unsigned long)synth_mmap);
            (void)mmap_count;
            break;
        }

        case MB2_TAG_FRAMEBUFFER: {
            struct {
                uint32_t type;
                uint32_t size;
                uint64_t addr;
                uint32_t pitch;
                uint32_t width;
                uint32_t height;
                uint8_t  bpp;
                uint8_t  fb_type;
            } *t = (void *)tag;

            synth_mbi.flags |= MBI_FLAG_FRAMEBUFFER;
            synth_mbi.framebuffer_addr  = t->addr;
            synth_mbi.framebuffer_pitch = t->pitch;
            synth_mbi.framebuffer_width = t->width;
            synth_mbi.framebuffer_height = t->height;
            synth_mbi.framebuffer_bpp   = t->bpp;
            synth_mbi.framebuffer_type  = t->fb_type;
            break;
        }

        default:
            break;
        }

        /* tag 按 8 字节对齐 */
        tag = (struct mb2_tag *)(((unsigned long)tag + tag->size + 7)
                                 & ~(unsigned long)7);
    }

    return &synth_mbi;
}

void kmain64(uint64_t magic, uint64_t mbi_addr)
{
    const struct multiboot_info *mbi;

    cli();
    ser_puts("\r\n[x64]entered ");

    if (magic != MB2_BOOT_MAGIC) {
        ser_puts("BAD-MAGIC\r\n");
        vga_clear();
        vga_color(0x0F);
        printf("YuanCore x86-64: bad multiboot2 magic 0x%llx\n",
               (unsigned long long)magic);
        for (;;)
            hlt();
    }

    ser_puts("magic-ok");
    mbi = parse_mb2(mbi_addr);
    ser_puts(" parse-ok\r\n");
    kernel_main_common(mbi);            /* 不返回 */
}
