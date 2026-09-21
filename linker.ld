/* linker.ld - YuanCore 内核内存布局
 *
 * 内核被 GRUB 加载到 1MB (0x100000) 处，这也是 Multiboot 规范的约定起点。
 * .multiboot 段必须放在最前面（GRUB 只在前 8KB 内搜索 Multiboot 头部）。
 */

ENTRY(start)

SECTIONS
{
    . = 1M;

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
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

    /* IDT 独占页：初始化后设为只读，任何对它的写都会在肇事者现场页错误 */
    .idtpage BLOCK(4K) : ALIGN(4K)
    {
        *(.idtpage)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /* 内核结束地址，后续做内存管理时可直接使用 */
    kernel_end = .;
}
