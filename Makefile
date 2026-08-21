# Makefile - 支持自动扫描 mode/ 下的 .c 文件
# 用法:
#   make         - 编译所有 .c 文件（包括 mode/ 下的）
#   make clean   - 清理
#   make run     - 运行

# 编译器与工具链
NASM    = nasm
GCC     = gcc
LD      = ld
GRUB    = grub-mkrescue
QEMU    = qemu-system-i386

# 编译参数
NASMFLAGS = -f elf32
CFLAGS    = -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 -MMD -MP
LDFLAGS   = -m elf_i386 -T linker.ld

# 自动收集所有 .c 文件（当前目录 + mode/ 目录）
SRCS_C   = $(wildcard *.c) $(wildcard mode/*.c)
OBJS     = $(SRCS_C:.c=.o) boot.o   # boot.o 单独处理
ELF      = mykernel.elf
ISO      = YuanCore.iso

# 默认目标
all: $(ISO)

# 链接
$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# 编译汇编
boot.o: boot.asm
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
	echo 'menuentry "YuanCore OS" {' >> iso/boot/grub/grub.cfg
	echo '  multiboot /boot/$(ELF)' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	$(GRUB) -o $@ iso/

# 清理
clean:
	rm -f $(OBJS) $(ELF) $(ISO) *.d mode/*.d   # 删除 .d 依赖文件
	rm -rf iso

# 运行
run: $(ISO)
	$(QEMU) -cdrom $(ISO)

# 包含自动生成的依赖文件（头文件变化时自动重编译）
-include $(SRCS_C:.c=.d)

.PHONY: all clean run