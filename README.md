# YuanCore（元核）

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**YuanCore** 是一个从零开始的 x86-64 业余操作系统内核，基于 GRUB Multiboot2 引导（BIOS 与 UEFI 双启动 ISO），使用 C 语言和 NASM 汇编编写。项目旨在提供一个简洁、可扩展的内核学习与开发平台。

> **当前状态**：x86-64 long mode 主线可用（V00.00.11[ALPHE]）——图形 GUI Shell、
> ring3 用户程序（开机自检）、YCP1 应用包、网络协议栈均已落地并在 QEMU 实机验证；
> i386 路径保留为遗留，开发重心在 x64。

---

## 📖 项目简介

YuanCore 是一个面向所有人的操作系统内核，旨在提供更现代化的体验和更加便捷的创作体验。

### ✨ 功能特性

**内核基础**

- ✅ GRUB Multiboot2 引导：`make x64` 产出 BIOS + EFI 双启动 `YuanCore64.iso`
- ✅ x86-64 long mode：32 位入口桩自建四级页表（2MB 大页恒等映射低 4GB）
  → PAE/LME/CR0.PG → ljmp；编译旗标 `-mno-red-zone -msoft-float`
- ✅ 中断体系：IDT64 + 中断桩 + PIC 重映射 + PIT 100Hz；
  IDT 只读页陷阱（运行期误写中断表即页错误点名肇事者）
- ✅ 内存管理：物理帧位图分配器 + 内核堆（kmalloc/kfree）+ 四级分页
- ✅ VGA 文本模式回退（帧缓冲不可用与 panic 路径）

**用户程序体系**

- ✅ **ring3 用户态（默认启用）**：TSS64 切栈、iretq 进入用户段、
  int 0x80 DPL3 系统调用、ELF64 加载器、用户态异常杀程序不崩内核；
  开机 smoke 包冒烟自检（yc_print + yc_exit 全链路往返）
- ✅ **系统调用 ABI 1.0**：31 项服务（打印/窗口/文件/鼠标/事件/节拍睡眠…），
  `apps/ycapi.h` 双架构自适应，契约见 `API_SPEC.md`
- ✅ **YCP1 应用包**：`.ycp` = stored zip + `ycmanifest.json` + 主目录；
  `ycp install / list / remove`，`/apps` 目录动态扫描注册表，
  开始菜单 / `apps` 命令 / 任务栏共用

**图形 Shell**

- ✅ **4×4 虚拟画布**（16 屏）：方向键 / 空白拖拽 / 雷达图点击三种导航，
  程序窗口以画布坐标摆放可跨屏，每屏带 (col,row) 屏号
- ✅ **右缘任务栏**：用户头像 / Start 菜单（打字即检索应用）/
  启动图标（Files/Terminal/Settings/About）/ 运行中程序栏（点击提升）/ 竖排时钟；
  Win 键唤起收起；**桌面不放图标**，启动入口全部收进任务栏
- ✅ **首启 OOBE** 管理员注册 + 登录守卫；开机官方 logo splash 动画
- ✅ 终端窗口可关闭/重开（历史输出不丢）
- ✅ 文件管理器：目录导航、查看文本文件、递归删除
- ✅ 窗口 UI 工具包：模态窗口 + 焦点管理 + 按钮/标签/输入框 +
  标题栏拖动（可贴边吸附）+ 滚动区/侧边选项卡控件
- ✅ 屏幕合成器：桌面/控制台/窗口/光标四层 z 序，按矩形局部重绘，光标零残影

**驱动与网络**

- ✅ PS/2 键盘（Set-1 + E0 扩展键，缓冲 int 化防截断）与鼠标（IRQ12 三字节包）
- ✅ RTC 实时时钟（UIP 等待 + 双读校验），任务栏时钟每秒局部刷新
- ✅ **网络协议栈**：PCI 枚举 → e1000 驱动 → ARP/IPv4/UDP →
  **NTP 时间同步**（`netinfo` / `ntpsync`，找不到网卡自动降级不阻塞）

**个性化**

- ✅ **主题系统**：23 个颜色槽 + 5 套预置（**default 即浅色** / ocean / sunset /
  matrix / light），运行时改色即时生效并持久化到 `/settings.cfg`
- ✅ 内存文件系统（ramfs）：`ls` / `cat` / `write` / `mkdir` / `rm` / `df`
- ✅ Shell 行编辑：光标移动、历史、Tab 补全、Ctrl+C/L/U、Esc

---

## 🛠️ 技术栈

| 组件 | 工具 | 说明 |
| :--- | :--- | :--- |
| 汇编器 | NASM (elf64) | 引导桩、中断桩、ring3 切换 |
| 编译器 | GCC (x86-64, freestanding) | `-mno-red-zone` 等旗标见 Makefile |
| 链接器 | GNU ld (elf_x86_64) | 内核与应用统一 ELF64 |
| 引导器 | GRUB2 (grub-mkrescue) | BIOS + EFI 双启动 ISO |
| 模拟器 | QEMU | 运行 / 无头截图 / gdb 调试 |
| 构建 | Make + Python 3 | `tools/make_ycp.py` 打包 YCP1 |

---

## 📂 目录结构

```
YuanCore/
├── kernel.c           # 内核主流程（两架构入口汇入 kernel_main_common）
├── Makefile           # make x64 / run64 / run-efi / yapps / shot / debug
├── x64/               # x86-64 主线
│   ├── boot32.asm     # Multiboot2 头 + 32 位入口桩 + 四级页表 + long mode
│   ├── kernel64.c     # kmain64：Multiboot2 tags 解析（合成 mb1 风格 info）
│   ├── gdt64/idt64/irq64/paging64.c
│   ├── isr64.asm / switch64.asm   # 陷阱帧镜像 + ring3 往返
│   └── exec64.c       # ELF64 加载器 + ring3 冒烟链路
├── mode/              # 内核模块
│   ├── desktop.c/.h   # 画布视口/任务栏/雷达图/UI 标准
│   ├── theme.c/.h     # 23 色槽 5 预置（default 浅色）
│   ├── ycp.c / zip.c  # YCP1 包安装/卸载（stored zip 解析）
│   ├── splash.c + logo_128.h      # 开机 logo（官方图形资产）
│   ├── fs.c / fsypk.c / pmm.c / heap.c / kbd.c / mouse.c / rtc.c / timer.c
│   └── *_blob.h       # 内置 YCP 包编译期内嵌（自动生成）
├── shell/             # shell.c console.c ui.c syscall.c registry.c users.c
│                      # + app_*.c（窗口程序）+ cmd_*.c（命令，扩展往这里加）
├── net/               # pci.c e1000.c net.c ntp.c（协议栈）
├── apps/              # 程序源码 + ycapi.h（程序侧 API 契约）
├── api-kit/           # 契约物料（头文件/文档打包）
└── tools/make_ycp.py  # YCP1 打包脚本（make yapps 调用）
```

---

## 🚀 构建与使用

### 前提条件

- **Linux**（推荐 Ubuntu）或 **Windows WSL2**
- 安装依赖：

```bash
sudo apt update && sudo apt install -y build-essential nasm \
    qemu-system-x86 grub-pc-bin grub-efi-amd64-bin mtools xorriso
```

### 编译与运行

```bash
git clone https://github.com/bamcmic/YuanCore.git
cd YuanCore

make x64        # 构建 YuanCore64.iso（BIOS+EFI 双启动）
make run64      # QEMU x86-64，BIOS 路径启动
make run-efi    # QEMU x86-64 + OVMF，UEFI 启动（需 ovmf 包）
```

### 常用命令

| 命令 | 说明 |
| :--- | :--- |
| `make x64` | 构建生成 `YuanCore64.iso`（主线） |
| `make run64` / `make run-efi` | QEMU 运行（BIOS / UEFI） |
| `make yapps` | 重打内置 YCP 包（demo / smoke） |
| `make shot` | 无头截图（不出窗口拿整屏 PPM） |
| `make debug` | QEMU -s -S，等 gdb 连 :1234 |
| `make clean` | 清理所有编译产物 |

> i386 遗留路径：`make` / `make run` 仍可构建旧 i386 `myos.iso`，功能冻结，
> 仅作架构对照。

---

## 📸 运行截图

[运行效果](https://private-user-images.githubusercontent.com/188480328/639501636-bd025583-0aa3-4caa-b5e8-255f434a3333.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3ODczMjEyNDQsIm5iZiI6MTc4NzMyMDk0NCwicGF0aCI6Ii8xODg0ODAzMjgvNjM5NTAxNjM2LWJkMDI1NTgzLTBhYTMtNGNhYS1iNWU4LTI1NWY0MzRhMzMzMy5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjYwODIxJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI2MDgyMVQxNDAyMjRaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT04ZDljZmE4ZmE1OGVjNTVjMDMxZjUzNTQ0NTRmOGZkYWE2NTQ5YjIxM2NkZGI2MmI2ZWU1ZDViZjZmMjk4YWIwJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCZyZXNwb25zZS1jb250ZW50LXR5cGU9aW1hZ2UlMkZwbmcifQ.gy3E6WgqwUPJYFo60AyTu_PsAqF0saEp5OkpOvIVHL8)

---

## ⚠️ 已知限制

- 单用户程序：同一时刻仅一个 ring3 程序（exec 同步执行），无调度器
- ramfs 掉电即失：`/apps`、`/settings.cfg` 开机重建（块设备驱动在路线图）
- ring3 隔离仅靠分页 user 位（四级 AND），无段级保护
- 网络栈为 e1000 轮询模型，主要在 QEMU user-net 环境验证；时区偏移未实现
- 窗口无最小化/最大化，重绘为整屏重排（脏矩形在路线图）
- i386 遗留路径仅 BIOS 引导，功能冻结于迁移前状态

---

## 🔜 后续计划

- [x] 中断体系 / 物理内存管理 / 图形桌面与交互式 shell
- [x] 分页 + ring3 用户态 + int 0x80 系统调用
- [x] x86-64 long mode 迁移（Multiboot2 + GRUB，BIOS+EFI 双启动）
- [x] x64 ring3 全链路（iretq / int 0x80 / ELF64 / 开机冒烟自检）
- [x] YCP1 应用包 + `/apps` 动态注册表
- [x] 网络协议栈（e1000 / ARP / IPv4 / UDP / NTP）
- [x] 4×4 虚拟画布 + 雷达图 + 任务栏启动图标（desktop v0.7，default 浅色）
- [ ] 多任务调度（当前单任务同步执行）
- [ ] 块设备驱动 + 可持久化文件系统
- [ ] 脏矩形重绘（合成器带宽优化）
- [ ] ring3 UI 仲裁（窗口管理器注册制）

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
