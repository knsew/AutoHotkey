# 原神专用精简版（本 fork 的 genshin/ 部分）

本仓库是 AutoHotkey 上游的 fork，**只在上游之外加了一个 `genshin/` 目录**，
上游源码（`source/` 等）原样保留，没有删、没有改。

## 精简的是“发行包”，不是仓库源码

Ji 2026-10-30 定：fork 后精简发行文件，只留解释器加连招。
落地口径如下，免得以后误会：

- **发行包（用户实际下载解压的东西）只含**：64 位解释器
  `AutoHotkey64.exe` + `license.txt` + `genshin/`（启动器、引擎、连招）。
- **发行包删掉的**：32 位解释器、帮助 chm、UX（Dash / WindowSpy / 安装器脚本）、
  Install.cmd、编译器等。
- **仓库里不删源码**，原因：GPLv2 下发行二进制时源码须可查；
  删了源码，这个 fork 就再也合不上上游更新了。
- 打包脚本：`tools/package-slim.py`，输入官方发行 zip，输出精简 zip，
  并自检包里没有 UX / chm / 32 位 / Install.cmd 才算通过。

## 启动器：Tauri 版是当前方向（Ji 2026-10-30 点选“用 Tauri 重做”）

见 `genshin-tauri/`：Rust 后端 + HTML/CSS/JS 前端，功能跟 C 版一致，
但界面层换了——标题、筛选条、底栏固定，列表是唯一滚动区，滚动条
只在列表区里，不会伸进筛选条（C 版窗口滚动条天生整窗高，分不开）。
构建见 `.github/workflows/build-tauri.yml`。

## 启动器：C 语言版（旧版，保留作对照，不再主推）

Ji 2026-10-30 补定：启动器用 C 语言写个精美一点的 GUI，给女孩子用，
不要 bat，双击 exe 运行，过程尽量简单；构建走 GitHub。

`genshin-launcher/launcher.c` 是纯 Win32 C 启动器（无第三方库）：
截图同款深紫渐变底 + 深色玻璃卡片 + 紫渐变触发键小药丸，标题“原神连招小助手”。
（截图与 segments.zip 里的文字切片仅作样式参照，未直接嵌进 exe：
切片分辨率低且只是文字，界面由 GDI 按截图样式重画；卡片滚动用裁剪区，
不再盖住顶栏和底栏。）
v6 新增：平滑滚动（滚轮/滚动条走 16ms 缓动动画 + 双缓冲，不闪）、
筛选条（全部 / ★收藏 / 已绑定 三个标签页 + 角色下拉 + 名字搜索 + 清除）、
卡片右上角 ★ 收藏（点星星只切换收藏，收藏存在 genshin/favorites.txt，
跟连招文件放一起，文件夹拷走就带走）、↑↓选卡、/ 聚焦搜索、Enter 改快捷键。
筛选条右上角显示“显示 X / 38 套 · 收藏 Y 套”。
图标是原创生成的紫色星星（launcher.ico，经 launcher.rc 嵌进 exe，
非官方素材）；卡片只在列表区内绘制，滚动时不再盖住顶栏和底栏。
易转库 28 套的角色与连招名已改中文（原名留在各 .combo 文件头的
“原名：”注释里，便于对照原仓库）。
它只做：列表展示 38 套连招、改快捷键（好看的 `Ctrl+1` 写法，改写
`.combo` 的 `@trigger`，引擎在运行时自动重载）、开始 / 停止连招、
打开连招文件夹。没有配置页、没有更新检查、没有其他管理功能。
真正打连招的是 `genshin/engine.ahk`。

入口变化：精简包根目录的 `GenshinLauncher.exe` 就是入口，不再有 bat，
旧 AHK 启动器 `genshin/launcher.ahk` 已删除。启动器自带
requireAdministrator 清单：双击时弹一次 UAC，之后按键才能发进游戏；
关闭窗口时连招也会一起停止。构建见
`.github/workflows/build-launcher.yml`（MSVC + /utf-8，产物是 Actions
artifact，不是 Release）。

## 用法

1. 推 `genshin-slim` 分支即触发 GitHub 构建，产物在 Actions 的
   `GenshinCombo-slim` artifact 里下载（不是 Release）。
2. 解压后双击 `GenshinLauncher.exe`（弹一次 UAC 属正常）。
3. 在启动器里点“开始连招”，切到原神窗口按快捷键触发。
4. 本地打包（备用，不含启动器 exe 时会告警）：
   `python3 tools/package-slim.py /tmp/AutoHotkey_2.0.31.zip [输出zip] [启动器exe]`

在 fork 仓库里直接开发时（没有 bin/ 解释器），需本机已装 AHK v2，
双击 `genshin/launcher.ahk` 即可，启动器会用已装的解释器拉引擎。

## 上游同步

上游默认分支是 `alpha`。本 fork 的原神改动放在 `genshin-slim` 分支，
不要直接往 `alpha` 上堆改动；上游更新时在 `genshin-slim` 上合 `alpha`
或 rebase，再重新跑打包脚本验证。

## 状态

- 连招：首期 38 套（必备 10 套 Ctrl+数字已绑定 + 易转库 28 套未绑定），
  来源与状态见 `genshin/SOURCES.md`，60 帧做不了的见 `genshin/docs/ABANDONED.md`。
- **未在真机 Windows 上跑过**：引擎、启动器、连招时序均未实测，
  首跑先只开 1 套低风险连招验证。宏类工具有封号风险。
- AutoHotkey 本体 GPLv2，见根目录 `license.txt`；连招里有第三方转换内容，
  出处保留在 `genshin/SOURCES.md`，不要当成原创对外发。
