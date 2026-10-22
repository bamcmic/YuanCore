/* x64/exec64.c - x86-64 程序加载器（ELF64 可执行）
 *
 * 与 i386 版 mode/exec.c 同一套契约(见 API_SPEC.md):
 *   - ELF64 ET_EXEC, e_machine=62, 链接基址 0x400000, 入口 app_main
 *   - PT_LOAD 段拷入恒等映射地址并清零 .bss
 *   - user_enter64 切 ring3, yc_exit(YC_EXIT) 经 int 0x80 回到本函数
 *
 * 长模式红利:中断帧格式 ring0/ring3 统一、内核态可用用户数据段寻址,
 * 因此 i386 版的用户区跳板不再需要 —— iretq 一步到位。
 *
 * 稳定性闸门保留:默认禁用(settings exp exec on 启用)。
 * 恒等映射无独立用户页表,程序越界踩内核是已知设计取舍(API_SPEC.md §2)。
 */
#include "../mode/exec.h"
#include "../mode/fs.h"
#include "../mode/heap.h"
#include "../mode/memmap.h"
#include "../mode/usermode.h"
#include "paging64.h"

/* ELF64 常量 */
#define ELF_MAGIC0 0x7F
#define PT_LOAD    1u
#define EM_X86_64  62u
#define ET_EXEC    2u

/* ELF64 头部关键字段偏移 */
#define E_ELFCLASS_OFF  4        /* 1=32位 2=64位 */
#define E_TYPE_OFF      0x10
#define E_MACHINE_OFF   0x12
#define E_ENTRY_OFF     0x18
#define E_PHOFF_OFF     0x20
#define E_PHENTSIZE_OFF 0x36
#define E_PHNUM_OFF     0x38

static unsigned short rd16(const unsigned char *p, unsigned int off)
{
    return (unsigned short)(p[off] | (p[off + 1] << 8));
}

static unsigned int rd32(const unsigned char *p, unsigned int off)
{
    return (unsigned int)p[off] | ((unsigned int)p[off + 1] << 8)
         | ((unsigned int)p[off + 2] << 16) | ((unsigned int)p[off + 3] << 24);
}

static unsigned long long rd64(const unsigned char *p, unsigned int off)
{
    return (unsigned long long)rd32(p, off)
         | ((unsigned long long)rd32(p, off + 4) << 32);
}

static int last_exit;

int exec_last_status(void) { return last_exit; }
void exec_set_exit(int code) { last_exit = code; }

/* 稳定性闸门:x64 ring3 链路已经开机 smoke 自检实机验证,
 * 默认启用;settings exp exec 仍可现场关闭(排查用)。 */
static int exec_enabled = 1;

int exec_enabled_get(void) { return exec_enabled; }
void exec_set_enabled(int on) { exec_enabled = (on != 0); }

int exec_run(const char *path)
{
    int size, i;
    unsigned char *img;
    unsigned long long entry, phoff;
    unsigned short phnum, phentsize;

    if (exec_enabled == 0)
        return EXEC_DISABLED;

    if (path == 0 || !fs_exists(path))
        return EXEC_NOTFOUND;

    size = fs_size(path);
    if (size < 64 || size > (int)MEM_EXEC_SIZE)
        return EXEC_TOOBIG;

    img = kmalloc((unsigned int)size);
    if (img == 0)
        return EXEC_ERR;
    if (fs_read(path, img, size) != size) {
        kfree(img);
        return EXEC_ERR;
    }

    /* 头部校验:ELF magic / 64 位 / x86-64 / ET_EXEC */
    if (img[0] != ELF_MAGIC0 || img[1] != 'E' || img[2] != 'L'
        || img[3] != 'F') {
        kfree(img);
        return EXEC_FORMAT;
    }
    if (img[E_ELFCLASS_OFF] != 2) {
        kfree(img);
        return EXEC_FORMAT;       /* ELF32 -> 用 i386 内核跑 */
    }
    if (rd16(img, E_MACHINE_OFF) != EM_X86_64
        || rd16(img, E_TYPE_OFF) != ET_EXEC) {
        kfree(img);
        return EXEC_FORMAT;
    }

    entry     = rd64(img, E_ENTRY_OFF);
    phoff     = rd64(img, E_PHOFF_OFF);
    phentsize = rd16(img, E_PHENTSIZE_OFF);
    phnum     = rd16(img, E_PHNUM_OFF);

    if (entry < MEM_EXEC_BASE
        || entry >= MEM_EXEC_BASE + MEM_EXEC_SIZE) {
        kfree(img);
        return EXEC_FORMAT;
    }

    for (i = 0; i < (int)phnum; i++) {
        const unsigned char *ph = img + (unsigned int)phoff
                                + (unsigned int)i * phentsize;
        unsigned int p_type   = rd32(ph, 0x00);
        unsigned int p_offset = rd32(ph, 0x08);
        unsigned long long p_vaddr  = rd64(ph, 0x10);
        unsigned long long p_filesz = rd64(ph, 0x20);
        unsigned long long p_memsz  = rd64(ph, 0x28);
        unsigned long long j;

        if (p_type != PT_LOAD)
            continue;
        if (p_vaddr < MEM_EXEC_BASE
            || p_vaddr + p_memsz > MEM_EXEC_BASE + MEM_EXEC_SIZE) {
            kfree(img);
            return EXEC_FORMAT;
        }

        for (j = 0; j < p_filesz; j++)
            ((volatile unsigned char *)(unsigned long)p_vaddr)[j]
                = img[p_offset + j];
        for (; j < p_memsz; j++)           /* .bss 清零 */
            ((volatile unsigned char *)(unsigned long)p_vaddr)[j] = 0;
    }

    kfree(img);

    /* 执行区 + 栈窗开用户访问位:恒等映射无独立用户页表,ring3 与
     * 内核同页表(API_SPEC.md §2 已知取舍)。0x400000-0x800000 全为
     * 保留空窗(memmap.h),沿途各级开 U/S 不暴露内核对象(U/S 为
     * 四级 AND,仅本窗 PD 项带 U/S);
     * 栈顶 0x800000 向下压栈落在 PD3 窗内。幂等:多次 exec 无副作用。 */
    paging64_set_user_range(MEM_EXEC_BASE, 0x400000ull);

    last_exit = -1;
    user_enter64(entry, MEM_USER_STACK_TOP);  /* 切 ring3,yc_exit 后回到这里 */
    return EXEC_OK;
}
