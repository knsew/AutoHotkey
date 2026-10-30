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

## 启动器：简单版，只管脚本和热键

`genshin/launcher.ahk` 是简单启动器，Ji 定“简单管理一下脚本和热键就行”，
所以它只做：列表展示 38 套连招、改热键（改写 `.combo` 的 `@trigger`）、
启动 / 停止 / 重载引擎、打开连招文件夹。没有配置页、没有更新检查、
没有其他管理功能。真正打连招的是 `genshin/engine.ahk`。

## 用法

1. 从官方 Release 下载对应版本的 `AutoHotkey_2.0.31.zip`（或更新版）。
2. 在仓库根目录跑：`python3 tools/package-slim.py /tmp/AutoHotkey_2.0.31.zip`
3. 解压生成的精简 zip，双击“启动原神连招.bat”。
4. 在启动器里点“启动引擎”，切到原神窗口按 Ctrl+数字触发。

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
