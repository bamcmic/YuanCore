/* linker64.ld - x86-64 内核内存布局(Multiboot2 + GRUB)
 *
 * Multiboot2 头必须在文件前 32KB 内(GRUB 搜索范围),
 * 放最前的独立段最稳妥。
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    .mb2 BLOCK(4K) : ALIGN(4K)
    {
        *(.mb2header)
    }

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    kernel_end = .;
}
