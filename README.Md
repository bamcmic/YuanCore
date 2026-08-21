# YuanCore（元核）

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**YuanCore** 是一个从零开始的 32 位 x86 操作系统内核，基于 GRUB Multiboot 引导，使用 C 语言和 NASM 汇编编写。项目旨在提供一个简洁、可扩展的内核学习与开发平台。

> ⚠️ **当前状态**：项目处于早期开发阶段，仅支持 i386 (32位) BIOS 引导，暂不支持 x86_64。

---

## 📖 项目简介

YuanCore 是一个面向所有人的操作系统内核，旨在提供更现代化的体验和更加便捷的创作体验。

### ✨ 功能特性

- ✅ GRUB Multiboot 引导支持
- ✅ 32 位保护模式
- ✅ VGA 文本模式输出（0xB8000）
- ✅ 简易 `printf` 字符串输出
- ✅ 支持 `mode/` 目录自动扩展编译
- ✅ 自动化 Makefile 构建
- ✅ 支持 QEMU 模拟运行

---

## 🛠️ 技术栈

| 组件 | 工具 | 说明 |
| :--- | :--- | :--- |
| 汇编器 | NASM | 编写引导代码 |
| 编译器 | GCC (i386-elf) | C 语言内核编译 |
| 链接器 | LD | ELF 格式链接 |
| 引导器 | GRUB | Multiboot 兼容引导 |
| 模拟器 | QEMU | 虚拟机运行测试 |
| 构建工具 | Make | 自动化构建流程 |

---

## 📂 目录结构

```
YuanCore/
├── boot.asm          # GRUB Multiboot 引导入口
├── kernel.c          # 内核主程序（C 语言）
├── linker.ld         # 链接脚本（内存布局）
├── Makefile          # 自动化构建
├── README.md         # 项目文档
├── LICENSE           # GPL-3.0 许可证
└── mode/             # 扩展模块目录（可添加更多 .c 文件）
```

---

## 🚀 构建与使用

### 前提条件

- **Linux**（推荐 Ubuntu）或 **Windows WSL2**
- 安装以下工具：

```bash
sudo apt update && sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso
```

### 编译与运行

```bash
# 克隆仓库
git clone https://github.com/你的用户名/YuanCore.git
cd YuanCore

# 编译并运行
make run
```

### 常用命令

| 命令 | 说明 |
| :--- | :--- |
| `make` | 编译生成 `myos.iso` |
| `make clean` | 清理所有编译产物 |
| `make run` | 在 QEMU 中运行内核 |
| `make clean && make run` | 完全重新构建并运行 |

---

## 📸 运行截图

<img src="https://img.remit.ee/api/file/BQACAgUAAyEGAASHRsPbAAEZmvFqiFok9IUypu59rpIm3OBthQq5NgACiy0AAgK8QFQH17X7SbuEZj0E.png" alt="BQACAgUAAyEGAASHRsPbAAEZmvFqiFok9IUypu59rpIm3OBthQq5NgACiy0AAgK8QFQH17X7SbuEZj0E.png" />

---

## ⚠️ 已知限制

- 仅支持 i386 (32位) BIOS 引导
- 暂不支持 x86_64 长模式
- 无中断处理（键盘/鼠标无响应）
- 无内存管理（未启用分页）
- 无文件系统支持

---

## 🔜 后续计划

- [ ] 添加键盘中断响应
- [ ] 支持 x86_64 长模式
- [ ] 实现简单内存管理（分页）
- [ ] 添加字符设备驱动
- [ ] 支持多任务调度
- [ ] UEFI 引导支持（通过 Limine）

---

## 📜 许可证

本项目使用 **GPL-3.0** 许可证开源。任何基于本项目的衍生作品也必须在相同的许可证下开源。

---

## 👨‍💻 作者

**bamcmic** — 操作系统爱好者

---

## 🙏 致谢

- [OSDev Wiki](https://wiki.osdev.org/) — 操作系统开发权威参考资料
- [GRUB Manual](https://www.gnu.org/software/grub/) — Multiboot 标准文档
- [QEMU Documentation](https://www.qemu.org/docs/master/) — 虚拟机调试工具

---

**Happy Hacking!** 🚀