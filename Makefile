# Makefile - 支持自动扫描 mode/ 下的 .c 文件
# 用法:
#   make         - 编译所有 .c 文件（包括 mode/ 下的）
#   make clean   - 清理
#   make run     - 运行
#   make debug   - 以 gdb 等待模式启动 QEMU

# 编译器与工具链（宿主 gcc + gcc-multilib，非交叉编译器）
NASM    = nasm
GCC     = gcc
LD      = ld
GRUB    = grub-mkrescue
QEMU    = qemu-system-i386

# 编译参数
# -fno-stack-protector : 内核无 __stack_chk_fail，必须关掉宿主从 gcc 默认开启的栈保护
# -fno-pie / -no-pie   : 内核是固定地址(1MB)映像，不能生成位置无关代码
NASMFLAGS = -f elf32
CFLAGS    = -m32 -ffreestanding -fno-pie -fno-stack-protector \
            -nostdlib -Wall -Wextra -O2 -MMD -MP
LDFLAGS   = -m elf_i386 -T linker.ld -no-pie

# ---- x86-64(EFI/长模式)目标 ----
# GRUB 引导保持不变:Multiboot2 头 + 32 位入口桩切长模式。
# ISO 同时含 BIOS 与 EFI 启动(需 grub-efi-amd64-bin + mtools)。
X64_QEMU    = qemu-system-x86_64
X64_NASM    = nasm -f elf64
X64_CFLAGS  = -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone \
              -mno-sse -mno-sse2 -mno-mmx -msoft-float -DYC_X64 \
              -I x64 -I mode \
              -nostdlib -Wall -Wextra -O2 -MMD -MP
X64_LDFLAGS = -m elf_x86_64 -T x64/linker64.ld -no-pie

# x64 不编译的模块:32 位中断框架与 ring3(未迁移,见 TECHNICAL.md)
X64_EXCLUDE = mode/gdt.c mode/idt.c mode/paging.c mode/tss.c mode/exec.c \
              mode/irq.c
X64_SRC_C   = $(filter-out $(X64_EXCLUDE),$(SRCS_C)) x64/kernel64.c \
              x64/gdt64.c x64/idt64.c x64/paging64.c x64/irq64.c x64/exec64.c
X64_SRC_ASM = x64/boot32.asm x64/isr64.asm
X64_OBJS    = $(X64_SRC_C:.c=.o64) $(X64_SRC_ASM:.asm=.o64)
X64_ELF     = mykernel64.elf
X64_ISO     = YuanCore64.iso

# 自动收集所有源文件
#   *.c / *.asm            内核本体
#   mode/                  内核模块
#   shell/                 用户态可见的命令行模块（加命令往这里放）
SRCS_C   = $(wildcard *.c) $(wildcard mode/*.c) $(wildcard shell/*.c)
SRCS_ASM = $(wildcard *.asm) $(wildcard mode/*.asm)
OBJS     = $(SRCS_C:.c=.o) $(SRCS_ASM:.asm=.o)
ELF      = mykernel.elf
ISO      = YuanCore.iso

# 默认目标
all: $(ISO)

# 目标说明
help:
	@echo "make        - 构建 YuanCore.iso"
	@echo "make run    - QEMU 弹窗运行（自动带 WSLg 兼容环境变量）"
	@echo "make shot   - 无头截图到 shot.ppm（无显示服务器时用）"
	@echo "make debug  - QEMU -s -S，等 gdb 连 :1234"
	@echo "make apps   - 编译示例程序 apps/hello.app（需 gcc-multilib）"
	@echo "make x64    - 构建 YuanCore64.iso（x86-64,GRUB BIOS+EFI 双启动）"
	@echo "make run64  - QEMU x86-64 运行（BIOS 路径）"
	@echo "make run-efi - QEMU x86-64 + OVMF 以 UEFI 启动（需 ovmf 包）"
	@echo "make clean  - 清理全部构建产物"

# ---- x64 规则 ----
%.o64: %.c
	$(GCC) $(X64_CFLAGS) -c $< -o $@

%.o64: %.asm
	$(X64_NASM) $< -o $@

$(X64_ELF): $(X64_OBJS)
	$(LD) $(X64_LDFLAGS) -o $@ $^

$(X64_ISO): $(X64_ELF)
	mkdir -p iso64/boot/grub
	cp $(X64_ELF) iso64/boot/
	echo 'set timeout=0' > iso64/boot/grub/grub.cfg
	echo 'set default=0' >> iso64/boot/grub/grub.cfg
	echo 'menuentry "YuanCore OS x86-64" {' >> iso64/boot/grub/grub.cfg
	echo '  multiboot2 /boot/$(X64_ELF)' >> iso64/boot/grub/grub.cfg
	echo '}' >> iso64/boot/grub/grub.cfg
	$(GRUB) -o $@ iso64/

x64: $(X64_ISO)

run64: $(X64_ISO)
	GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 DISPLAY=$${DISPLAY:-:0} \
		$(X64_QEMU) -m 256 -cdrom $(X64_ISO)

# OVMF 固件路径按 Debian/Ubuntu 包默认;不同发行版可用 EFI_D= 指定
EFI_D ?= /usr/share/OVMF
run-efi: $(X64_ISO)
	GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 DISPLAY=$${DISPLAY:-:0} \
		$(X64_QEMU) -m 256 -M q35 \
		-drive if=pflash,format=raw,readonly=on,file=$(EFI_D)/OVMF_CODE.fd \
		-drive if=pflash,format=raw,file=$(EFI_D)/OVMF_VARS.fd \
		-cdrom $(X64_ISO)

# 链接
$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# 编译汇编（boot.asm 与 mode/*.asm 都走这条规则）
%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

# 编译 C 文件（通用规则）
%.o: %.c
	$(GCC) $(CFLAGS) -c $< -o $@

# 生成 ISO
$(ISO): $(ELF)
	mkdir -p iso/boot/grub
	cp $(ELF) iso/boot/
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'insmod vbe' >> iso/boot/grub/grub.cfg
	echo 'menuentry "YuanCore OS" {' >> iso/boot/grub/grub.cfg
	echo '  multiboot /boot/$(ELF)' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	$(GRUB) -o $@ iso/

# 用户程序（ELF32，基址 4MB，入口 app_main；见 apps/ycapi.h）
# -mno-sse/-mno-sse2/-mno-mmx：qemu32 CPU 没有 SSE2，clang/gcc 若编出
# SSE 指令，用户态第一条就 #UD（曾在 hello 上实机踩过）
APP_CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
             -mno-sse -mno-sse2 -mno-mmx -msoft-float

apps: apps/hello.app apps/closeall.app

apps/hello.o: apps/hello.c apps/ycapi.h
	$(GCC) $(APP_CFLAGS) -c $< -o $@

apps/hello.app: apps/hello.o
	$(LD) -m elf_i386 -e app_main -Ttext-segment=0x400000 -o $@ apps/hello.o

apps/closeall.o: apps/closeall.c apps/ycapi.h
	$(GCC) $(APP_CFLAGS) -c $< -o $@

apps/closeall.app: apps/closeall.o
	$(LD) -m elf_i386 -e app_main -Ttext-segment=0x400000 -o $@ apps/closeall.o

# 清理
clean:
	rm -f $(OBJS) $(ELF) $(ISO) *.d mode/*.d shell/*.d print.o
	rm -f $(X64_OBJS) $(X64_ELF) $(X64_ISO) x64/*.d *.o64
	rm -rf iso iso64

# 运行
# WSLg 下有两个坑，这里一并绕开：
#   1) 非登录 shell 里 DISPLAY 可能为空 → 兜底为 :0（WSLg 的 X server 就是 :0）
#   2) WAYLAND_DISPLAY 存在时 GTK 默认选 Wayland 后端，窗口只会在任务栏留图标、
#      内容渲染不出来 → 强制 GDK_BACKEND=x11 走 Xwayland
#      （同时给 SDL 兜底，防止 QEMU 用的是 SDL 后端）
run: $(ISO)
	GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 DISPLAY=$${DISPLAY:-:0} $(QEMU) -cdrom $(ISO)

# 指令级追踪 —— 复现内核崩溃时用：弹出窗口正常玩，崩溃后回终端敲 quit，
# 生成的 qemu.log 里有崩溃前最后的中断/指令记录（发给 AI 定位）。
trace: $(ISO)
	GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 DISPLAY=$${DISPLAY:-:0} \
		$(QEMU) -cdrom $(ISO) -d int -D qemu.log -monitor stdio

# 无头截图 —— WSL 没有显示服务器（无 WSLg / X server）时用这个代替 make run。
# QEMU 不弹窗，靠 monitor 的 screendump 把显存整屏导成 PPM。
SHOT = shot.ppm
shot: $(ISO)
	rm -f $(SHOT)
	( sleep 6; echo "screendump $(SHOT)"; sleep 2; echo "quit" ) | \
		$(QEMU) -cdrom $(ISO) -display none -monitor stdio -m 128
	@test -s $(SHOT) && echo "已生成 $(SHOT)" || echo "截图失败：$(SHOT) 为空或不存在"

# 诊断显示环境（WSLg 是否可用、DISPLAY 是否设置）
wslg-check:
	@echo "DISPLAY         = [$${DISPLAY}]"
	@echo "WAYLAND_DISPLAY = [$${WAYLAND_DISPLAY}]"
	@echo "XDG_RUNTIME_DIR = [$${XDG_RUNTIME_DIR}]"
	@echo -n "X socket        : "; ls /tmp/.X11-unix 2>/dev/null || echo "(无)"
	@echo -n "WSLg            : "; test -d /mnt/wslg && echo "已挂载 (/mnt/wslg)" || echo "未挂载"

# 调试：QEMU 在 1234 端口等待 gdb 连接
debug: $(ISO)
	GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 DISPLAY=$${DISPLAY:-:0} $(QEMU) -cdrom $(ISO) -s -S

# 包含自动生成的依赖文件（头文件变化时自动重编译）
-include $(SRCS_C:.c=.d)

.PHONY: all clean run debug shot wslg-check help x64 run64 run-efi apps
