# YuanCore

**随心所调，随意所控**

YuanCore 是一个从零开始的 32 位 x86 操作系统项目:多任务图形桌面、ring3 用户程序、
ELF 加载器、系统调用、文件系统、鼠标与窗口系统,全部手写,无 libc、无第三方内核依赖。

> 当前为**稳定优先**版本:桌面、任务栏、窗口系统、文件管理器、设置均已实机验证;
> ring3 程序执行为实验性功能,默认关闭(见下文"实验性选项")。

## 功能总览

### 内核

- GRUB Multiboot 引导,Multiboot video 请求 **1024x768x32** 线性帧缓冲
- GDT / TSS / IDT / 8259 PIC / PIT 时钟(100Hz)
- **分页**:内核 / 用户 / 堆 / 帧缓冲分区映射,user 位即保护边界
- **ring3 用户态**:iret 跳板切换,用户态异常杀程序不崩内核(实验性,默认禁用)
- **int 0x80 系统调用**:28 个服务(打印/窗口/输入/文件/鼠标/随机数/应用注册表…)
- 帧位图物理内存管理(PMM)+ 内核堆(kmalloc/kfree)
- PS/2 键盘(方向键/Ctrl 组合键/Win 键)与 PS/2 鼠标(IRQ12)
- RTC 实时时钟;图形模式红屏 panic(全寄存器 + CR2 + 内核栈转储)
- IDT 只读陷阱(CR0.WP):中断表被踩时在肇事指令现场页错误

### 图形与桌面

- 屏幕合成器:桌面 → 控制台 → 窗口 → 光标四层 z 序,按矩形局部合成,光标零残影
- 8x16 点阵字体(Consolas 光栅化,95 字形)
- 桌面图标、可拖动窗口、可关闭的终端窗口
- **右侧竖放任务栏**:Start 在顶、竖排时钟在底,Win 键唤起/收起
- 开始菜单:**应用检索**(打字即过滤,Enter 启动)
- 主题引擎:18 个颜色项 + 5 套预置(default/ocean/sunset/matrix/light),
  运行时改色即时生效并持久化到 `/settings.cfg`

### Shell 与应用

- 行编辑:光标移动、历史(↑↓)、Tab 补全、Ctrl+C/L/U
- 命令:help / clear / about / mem / uptime / date / ticks / echo / apps /
  ls / cat / write / mkdir / rm / df / mouse / run / settings / reboot
- 图形化应用:文件管理器(目录导航 + 文本查看)、设置(Theme / Experimental 选项卡)、
  About、开始菜单
- 内存文件系统(ramfs):目录树 + 堆存储
- 内置程序:hello(计数窗口)、closeall(关闭所有窗口)

## 快速开始

需要 WSL(Ubuntu,装好 `gcc-multilib nasm grub-pc-bin xorriso qemu-system-i386`):

```bash
cd ~/YuanCore
make run      # 构建并启动 QEMU
```

其它目标:`make shot`(无头截图)、`make apps`(重编用户程序)、`make debug`、`make help`。

## 操作速览

| 操作 | 方式 |
|---|---|
| 唤起/收起任务栏 | **Win 键** 或点屏幕右缘 |
| 移动窗口 | 按住标题栏拖动 |
| 关闭窗口 | × 或 Esc |
| 开始菜单检索 | 打开菜单后直接打字 |

完整说明见 [OPERATIONS.md](OPERATIONS.md)(操作手册),
架构与实现细节见 [TECHNICAL.md](TECHNICAL.md)(开发文档)。

## 实验性选项

`settings exp` 命令或 设置窗口 → Experimental 选项卡:

| 选项 | 默认 | 说明 |
|---|---|---|
| `exec` | off | ring3 程序执行(run / 开始菜单启动项)。有未定位根因的稳定性问题,默认禁用 |
| `refresh` | off | 全局刷新 ~30fps(整屏重绘,性能开销大,诊断/演示用) |
| `drag` | on | 窗口拖动 |
| `snap` | off | 拖动贴边吸附 |
| `seconds` | on | 时钟秒行 |

## 写一个程序

用户程序 = 标准 ELF32(基址 4MB,入口 `app_main`),经 `apps/ycapi.h` 调用 28 个
系统服务。示例见 `apps/hello.c`。**编译必须带 `-mno-sse -mno-sse2 -mno-mmx -msoft-float`**
(QEMU i386 默认 CPU 无 SSE2,详见 ycapi.h 顶部注释)。

## 项目结构

```
boot.asm   kernel.c   linker.ld   Makefile
mode/      内核本体:gdt idt irq paging tss pmm heap fs mouse kbd rtc
           fb font theme desktop print memmap exec …
shell/     命令行:shell console ui registry exp syscall
           命令模块 cmd_*、窗口应用 app_*、内置程序 blob
apps/      用户程序源码(hello / closeall)与程序侧 API(ycapi.h)
```

## 文档

- [TECHNICAL.md](TECHNICAL.md) — 架构、启动流程、内存布局、中断体系、合成器、系统调用表
- [OPERATIONS.md](OPERATIONS.md) — 操作手册、命令、快捷键、故障排查

## 许可

见 [LICENSE](LICENSE)。
