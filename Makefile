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
	@echo "make clean  - 清理全部构建产物"

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
apps: apps/hello.app

apps/hello.o: apps/hello.c apps/ycapi.h
	$(GCC) -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -c $< -o $@

apps/hello.app: apps/hello.o
	$(LD) -m elf_i386 -e app_main -Ttext-segment=0x400000 -o $@ apps/hello.o

# 清理
clean:
	rm -f $(OBJS) $(ELF) $(ISO) *.d mode/*.d shell/*.d print.o
	rm -rf iso

# 运行
# WSLg 下有两个坑，这里一并绕开：
#   1) 非登录 shell 里 DISPLAY 可能为空 → 兜底为 :0（WSLg 的 X server 就是 :0）
#   2) WAYLAND_DISPLAY 存在时 GTK 默认选 Wayland 后端，窗口只会在任务栏留图标、
#      内容渲染不出来 → 强制 GDK_BACKEND=x11 走 Xwayland
#      （同时给 SDL 兜底，防止 QEMU 用的是 SDL 后端）
run: $(ISO)
	GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 DISPLAY=$${DISPLAY:-:0} $(QEMU) -cdrom $(ISO)

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

.PHONY: all clean run debug shot wslg-check help apps
