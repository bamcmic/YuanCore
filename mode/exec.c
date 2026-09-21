/* exec.c - 程序加载器（ELF32 可执行文件）
 *
 * "可执行文件"就是一个标准的 ELF32 ET_EXEC：
 *   - 链接基址 = MEM_EXEC_BASE(4MB)，入口符号 app_main
 *   - 加载器解析 program header，把 PT_LOAD 段拷到对应虚拟地址并清零 .bss
 *   - 然后跳到 e_entry
 *
 * 构建（WSL，gcc）：
 *   gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
 *       -c hello.c -o hello.o
 *   ld -m elf_i386 -e app_main -Ttext-segment=0x400000 -o hello.app hello.o
 * 或用 zig：
 *   zig cc -target x86-freestanding -ffreestanding -nostdlib -O2 \
 *       -Wl,--image-base=0x400000 -Wl,-e,app_main -o hello.app hello.c
 *
 * 程序通过固定地址的系统调用表(MEM_SYSCALL_BASE)调用内核服务，
 * 见 apps/ycapi.h。
 */
#include "exec.h"
#include "fs.h"
#include "heap.h"
#include "memmap.h"
#include "usermode.h"

/* ELF 常量 */
#define ELF_MAGIC0 0x7F
#define PT_LOAD    1u
#define EM_386     3u
#define ET_EXEC    2u

static unsigned short rd16(const unsigned char *p, unsigned int off)
{
    return (unsigned short)(p[off] | (p[off + 1] << 8));
}

static unsigned int rd32(const unsigned char *p, unsigned int off)
{
    return (unsigned int)p[off] | ((unsigned int)p[off + 1] << 8)
         | ((unsigned int)p[off + 2] << 16) | ((unsigned int)p[off + 3] << 24);
}

static int last_exit;

int exec_last_status(void) { return last_exit; }
void exec_set_exit(int code) { last_exit = code; }

/* 稳定性闸门：ring3 切换路径存在两次未定位根因的内核态踩踏史
 * （IDT 门被覆盖，见 TECHNICAL.md）。默认禁用，settings exp exec on 启用。 */
static int exec_enabled;

int exec_enabled_get(void) { return exec_enabled; }
void exec_set_enabled(int on) { exec_enabled = (on != 0); }

int exec_run(const char *path)
{
    int size, i;
    unsigned char *img;
    unsigned int entry, phoff, phnum, phentsize;

    if (exec_enabled == 0)
        return EXEC_DISABLED;

    if (!fs_exists(path))
        return EXEC_NOTFOUND;

    size = fs_size(path);
    if (size < 52 || size > (int)MEM_EXEC_SIZE)
        return EXEC_TOOBIG;

    img = kmalloc((unsigned int)size);
    if (img == 0)
        return EXEC_ERR;
    if (fs_read(path, img, size) != size) {
        kfree(img);
        return EXEC_ERR;
    }

    if (img[0] != ELF_MAGIC0 || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') {
        kfree(img);
        return EXEC_FORMAT;
    }
    if (rd16(img, 0x12) != EM_386 || rd16(img, 0x10) != ET_EXEC) {
        kfree(img);
        return EXEC_FORMAT;
    }

    entry     = rd32(img, 0x18);
    phoff     = rd32(img, 0x1C);
    phentsize = rd16(img, 0x2A);
    phnum     = rd16(img, 0x2C);

    if (entry < MEM_EXEC_BASE || entry >= MEM_EXEC_BASE + MEM_EXEC_SIZE) {
        kfree(img);
        return EXEC_FORMAT;
    }

    for (i = 0; i < (int)phnum; i++) {
        const unsigned char *ph = img + phoff + (unsigned int)i * phentsize;
        unsigned int p_type   = rd32(ph, 0x00);
        unsigned int p_offset = rd32(ph, 0x04);
        unsigned int p_vaddr  = rd32(ph, 0x08);
        unsigned int p_filesz = rd32(ph, 0x10);
        unsigned int p_memsz  = rd32(ph, 0x14);
        unsigned int j;

        if (p_type != PT_LOAD)
            continue;
        if (p_vaddr < MEM_EXEC_BASE
            || p_vaddr + p_memsz > MEM_EXEC_BASE + MEM_EXEC_SIZE) {
            kfree(img);
            return EXEC_FORMAT;
        }

        for (j = 0; j < p_filesz; j++)
            ((volatile unsigned char *)p_vaddr)[j] = img[p_offset + j];
        for (; j < p_memsz; j++)               /* .bss 清零 */
            ((volatile unsigned char *)p_vaddr)[j] = 0;
    }

    kfree(img);

    /* 拷贝 ring3 入口跳板到用户区 */
    {
        volatile unsigned char *tr = (volatile unsigned char *)MEM_USER_TRAMPOLINE;
        tr[0] = 0x1F;             /* pop ds  */
        tr[1] = 0x07;             /* pop es  */
        tr[2] = 0x0F; tr[3] = 0xA1;   /* pop fs  */
        tr[4] = 0x0F; tr[5] = 0xA9;   /* pop gs  */
        tr[6] = 0xFF; tr[7] = 0xE0;   /* jmp eax */
    }

    last_exit = -1;
    user_enter(entry, MEM_USER_STACK_TOP);   /* 切 ring3，yc_exit 后回到这里 */
    return EXEC_OK;
}

