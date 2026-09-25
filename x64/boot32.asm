; boot32.asm - x86-64 版引导入口(Multiboot2 + GRUB)
;
; GRUB 无论 BIOS 还是 EFI 启动,都以 32 位保护模式把控制权交给
; multiboot2 入口(EAX=0x36D76289, EBX=MBI 物理地址)。
; 本桩完成:
;   1. 校验魔数并保存 MBI 地址
;   2. 构建四级页表,恒等映射低 4GB(2MB 大页 × 2048)
;   3. 开启 PAE + EFER.LME + CR0.PG 进入长模式
;   4. 载入 64 位 GDT,ljmp 进入 64 位代码,调 kmain64
;
; 页表/GDT/栈都是本文件的静态数组(低 4GB 内,恒等映射下天然可用)。

MB2_MAGIC   equ 0xE85250D6
MB2_ARCH    equ 0                       ; i386 保护模式入口
MB2_HDR_LEN equ (mb2_end - mb2_start)
MB2_CHKSUM  equ -(MB2_MAGIC + MB2_ARCH + MB2_HDR_LEN)

MB2_BOOT_MAGIC equ 0x36D76289

STACK64_SIZE equ 16384

%macro SEROUT 1              ; COM1 调试信标:直写 THR,QEMU 串口不校验波特率
    mov dx, 0x3F8
    mov al, %1
    out dx, al
%endmacro

[bits 32]

section .mb2header
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_HDR_LEN
    dd MB2_CHKSUM

    ; 信息请求:告诉 GRUB 我们需要 framebuffer / memory map / basic meminfo
    ; (编号为引导信息 tag 类型:framebuffer=8, mmap=6, meminfo=4;
    ;  size 必须含 type+size 自身 = 2+2+4*3 = 16,GRUB 严格校验)
    align 8
    dw 1                        ; type = information request
    dw 16
    dd 8                        ; framebuffer info
    dd 6                        ; memory map
    dd 4                        ; basic meminfo

    ; 帧缓冲请求:1024x768x32 线性帧缓冲(size = 2+2+4*3 = 16)
    align 8
    dw 5                        ; type = framebuffer
    dw 16
    dd 1024
    dd 768
    dd 32

    ; 结束 tag(size 固定为 8)
    align 8
    dw 0
    dw 8
mb2_end:

section .bss
align 4096
pml4:   resd 1024               ; PML4(实际只用第 0 项)
align 4096
pdpt:   resd 1024               ; PDPT(实际只用前 4 项,2MB 页覆盖 4GB)
align 4096
pd:     resd 4096               ; 4 张连续页目录(每张 512 项) = 2048 × 2MB = 4GB
align 16
mbi_ptr: resd 1                 ; GRUB 传来的 MBI 物理地址
stack64: resb STACK64_SIZE
stack64_top:

section .data
align 16
gdt64:
    dq 0                        ; null
    dq 0x00AF9A000000FFFF       ; 64-bit code: base 0, limit 4G, P|S|EXE|L
    dq 0x00CF92000000FFFF       ; 64-bit data: base 0, limit 4G, P|S|W
gdt64_end:
gdt64_desc:
    dw (gdt64_end - gdt64 - 1)
    dd gdt64

section .text
[bits 32]
global _start
extern kmain64

_start:
    cli
    cmp eax, MB2_BOOT_MAGIC
    jne .halt
    SEROUT 'B'                  ; magic 校验通过

    mov [mbi_ptr], ebx

    ; ---- 构建页表:PML4[0] -> PDPT[0..3] -> 4 张 PD(各 512 项) x 2MB = 恒等映射 0..4GB ----
    ; (PDPT 单项覆盖 1GB,长模式 PD 仅 512 项;帧缓冲 0xFD000000 在 PDPT[3])
    mov eax, pdpt
    or  eax, 0x03               ; PRESENT | RW
    mov [pml4], eax             ; PML4[0] -> PDPT
    mov dword [pml4 + 4], 0

    ; 长模式每张 PD 只有 512 项(9 位索引),512 x 2MB = 1GB,
    ; 因此覆盖 4GB 需要 4 张连续 PD(16KB,pd 数组正好) + PDPT 四项。
    mov eax, pd
    or  eax, 0x03               ; PRESENT | RW
    mov [pdpt], eax             ; PDPT[0] -> 0..1GB
    add eax, 4096
    mov [pdpt + 8], eax         ; PDPT[1] -> 1..2GB
    add eax, 4096
    mov [pdpt + 16], eax        ; PDPT[2] -> 2..3GB
    add eax, 4096
    mov [pdpt + 24], eax        ; PDPT[3] -> 3..4GB(帧缓冲 0xFD000000 在此)
    mov dword [pdpt + 4], 0
    mov dword [pdpt + 12], 0
    mov dword [pdpt + 20], 0
    mov dword [pdpt + 28], 0

    mov edi, 0                  ; 物理地址游标(2MB 步进)
    mov esi, pd                 ; 表项游标
.fill:
    mov [esi], edi              ; 低 32 位 = 帧基址(2MB 对齐,低 21 位为 0)
    mov dword [esi + 4], 0      ; 高 32 位 = 0(<4GB)
    or  dword [esi], 0x83       ; PRESENT | RW | PS(2MB 大页)
    add edi, 0x200000
    add esi, 8
    test edi, edi               ; edi 回绕到 0 = 已映射满 4GB(2048 项)
    jnz .fill
    SEROUT 'P'                  ; 页表构建完成

    ; ---- 开长模式 ----
    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or  eax, 0x20               ; CR4.PAE
    mov cr4, eax

    mov ecx, 0xC0000080         ; MSR_EFER
    rdmsr
    or  eax, 0x100              ; EFER.LME
    wrmsr

    mov eax, cr0
    or  eax, 0x80010000         ; CR0.PG + CR0.WP(ring0 写只读页也页错误)
    mov cr0, eax                ; 此刻进入兼容模式下的 active long mode

    lgdt [gdt64_desc]
    jmp 0x08:entry64            ; 装入 64 位代码段

.halt:
    hlt
    jmp .halt

[bits 64]
entry64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack64_top
    xor rbp, rbp
    SEROUT 'L'                  ; 已进入 64 位长模式

    ; SysV ABI:kmain64(magic, mbi_addr) → rdi = magic,rsi = mbi_addr
    ; (32 位立即数写 edi/esi 自动零扩展到 64 位)
    mov edi, MB2_BOOT_MAGIC
    mov esi, [mbi_ptr]
    SEROUT 'K'                  ; 即将调用 kmain64
    call kmain64

.h:
    hlt
    jmp .h

section .note.GNU-stack noalloc noexec nowrite progbits
