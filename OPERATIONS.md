# YuanCore 操作手册

面向日常使用。开发与架构见 `TECHNICAL.md`。

## 1. 构建与启动

在 WSL(Ubuntu) 里：

```bash
cd ~/myos
sudo apt install grub-efi-amd64-bin mtools ovmf   # 一次性依赖
make x64        # 主线:构建 YuanCore64.iso(BIOS+EFI 双启动)
make run64      # QEMU x86-64,BIOS 路径启动(需要 WSLg 显示窗口)
make run-efi    # QEMU x86-64 + OVMF,UEFI 启动
make shot       # 无头模式:不弹窗,跑完生成 shot.ppm 截图
make apps       # 只重编用户程序 ELF(x64 旗标)
make yapps      # 重打 YCP1 应用包(内嵌 blob)
make help       # 列出全部目标
```

i386 旧目标 `make run` 仍可用，仅作对照。依赖缺失时 `make x64` 会明确报错。
x64 版 ring3 程序执行默认开启（见"实验性选项"的 exec）。

`make run` 窗口不出来时，先 `wsl --shutdown`(Windows PowerShell)再试；
仍不行用 `make shot` 验证内核本身，或 `-vnc :1` + VNC 客户端。

## 2. 桌面

| 元素 | 说明 |
|---|---|
| 桌面画布 | 4×4 共 16 屏的虚拟桌面；每屏左下角有屏号，窗口可跨屏摆放 |
| 任务栏 | 右缘竖条(默认显示)：头像(点击=设置)/Start/启动图标/运行程序栏/时钟。Win 键或点右缘 4px 提示条**唤起/收起** |
| Start 按钮 | 打开开始菜单 |
| 启动图标 | 任务栏 Start 之下：Files / Terminal / Settings / About，单击打开(v0.7 起桌面不放图标) |
| 雷达图 | 右下角 minimap：显示 16 屏与当前视口位置，点中即跳屏 |
| 终端窗口 | YuanCore Shell，右上角 × 可**关闭**；Terminal 图标或菜单重开，历史输出不丢 |

**画布导航**：方向键按屏移动视口 / 空白画布按住拖动平移 / 点雷达图跳屏。

**开始菜单**：顶部即搜索框，直接打字即检索应用(如输 `he` 过滤出 hello)；
↑↓ 或 Tab 选择，Enter 启动；Esc 关闭。下方固定入口:
Files / Terminal / Settings / About / Reboot / Close。

## 3. 窗口操作

| 操作 | 方式 |
|---|---|
| 移动窗口 | **按住标题栏拖动**(鼠标) |
| 关闭窗口 | × 按钮 或 Esc |
| 切换焦点 | Tab / 方向键 |
| 触发按钮 | Enter(键盘) / 单击(鼠标) |

## 4. 键盘速查

| 键 | 作用 |
|---|---|
| **Win** | 唤起/收起任务栏 |
| ↑/↓/←/→ | shell:翻命令历史(↑/↓)；窗口:移动焦点；无窗口时:画布按屏平移 |
| Tab | shell:命令补全；窗口:下一焦点 |
| Home/End | 行首/行尾 |
| Ctrl+C / Ctrl+U | 放弃当前行 / 清空当前行 |
| Ctrl+L | 清屏 |
| Esc | 关闭当前窗口 / 清空输入行 |

## 5. Shell 命令

```
help                  命令列表          clear     清屏
about                 内核信息          mem       内存/堆统计
uptime / ticks        开机时长/tick      date      RTC 实时时间
reboot                重启              apps      已安装应用列表
ls [目录]  cat <文件>  write <文件> <文本>
mkdir <目录>  rm <路径>  df
mouse                 鼠标事件实时监视(Esc 退出)
run <程序>            启动程序(x64 默认开启)
ycp install|list|remove   YCP1 应用包安装/列表/卸载
netinfo               网卡/MAC/IP 信息
ntpsync               NTP 对时(QEMU user-net)
settings              UI 设置(见下)
```

## 6. 个性化设置

```
settings                        列出 23 个颜色项
settings set <key> <RRGGBB>     改单项(如 settings set accent FF0000)
settings preset <name>          换主题(default=浅色/ocean/sunset/matrix/light)
settings wallpaper gradient|solid|dots    壁纸样式
settings clock on|off           任务栏时钟开关
settings ui                     图形化设置窗口(Theme / Experimental 选项卡)
settings exp                    实验性选项列表(见下)
settings exp <name> on|off      切换实验性选项
settings reset                  恢复默认
```

改动即时生效并自动保存到 `/settings.cfg`(内存文件系统，重启回默认)。

### 实验性选项(`settings exp` / 设置窗口 Experimental 选项卡)

| 选项 | 默认 | 说明 |
|---|---|---|
| `exec` | **on** | **ring3 程序执行**（`run` / 开始菜单启动项）。x64 内核默认启用；遇问题可临时关闭排障 |
| `refresh` | off | **全局刷新 ~30fps**:整屏每秒重绘 30 次。性能开销极大,QEMU 下会明显拖慢交互,仅诊断/演示用 |
| `drag` | on | 窗口拖动总开关(关掉后标题栏不能拖) |
| `snap` | off | 拖动时窗口距屏缘 16px 内自动吸齐 |
| `seconds` | on | 任务栏时钟秒显示(on=HH/MM/SS 三行，off=两行) |

图形化方式:`settings ui` 打开设置窗口 → Experimental 选项卡,
方向键/鼠标选中条目,Enter 切换开关。

实验性选项**只存内存**，重启回默认——稳定后才会并入正式设置并持久化。

## 7. 文件与程序

- `ls` / Files 图标浏览 ramfs；`cat` / Files 里 Enter 查看/编辑文本文件。
- `write /notes.txt hello world` 建文件；`rm` 删除(目录递归)；`mkdir` 建目录。
- 应用以 **YCP1 包**(`.ycp`)分发，装在 `/apps/<名字>/`；内置 demo、smoke
  两个包，开机自动安装。`apps` 命令列表，开始菜单检索启动。
- 安装自己的包:`ycp install <路径>`；卸载:`ycp remove <名字>`。
- 自己写程序:参考 `apps/ycapi.h`(程序侧 API)与 `apps/counter.c`(范例)，
  `make apps` 编译 ELF，`make yapps` 打包。
- **编译用户程序必须带 `-mno-red-zone -mno-sse -mno-sse2 -mno-mmx -msoft-float`**:
  QEMU 默认 CPU 没有 SSE2，编出 SSE 指令用户态直接 #UD 崩溃
  (make apps 已内置这些标志)。

## 8. 程序可用 API(int 0x80，31 项 / ABI 1.0)

打印/绘图:`yc_print yc_label yc_button yc_fillrect yc_putpixel`
输入:`yc_getkey yc_key_poll yc_mouse_poll yc_wait_event yc_cursor`
窗口:`yc_draw_window yc_refresh yc_close_all yc_term_show`
文件:`yc_read_file yc_write_file yc_dir_list yc_delete`
系统:`yc_random yc_ticks yc_sleep_ticks yc_app_count yc_app_name yc_app_desc yc_abi yc_exit`

完整调用号表与构建命令见 `apps/ycapi.h`，契约规范见 `API_SPEC.md`。

## 9. 故障排查

| 现象 | 处置 |
|---|---|
| QEMU 无窗口 | `wsl --shutdown` 后重试;或 `make shot` 无头验证 |
| 红屏 PANIC | 拍照。cr2=出错地址，err 含 WRITE/READ，寄存器与栈可定位肇事代码 |
| `[!!!] IDT CORRUPTED` | 内核自检发现中断表被踩，请反馈 |
| 打字无反应 | 看终端窗口是否被关闭(Terminal 图标重开) |
| 时钟是 UTC | QEMU 硬件钟即 UTC，时区偏移尚未实现 |
