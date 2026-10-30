#!/usr/bin/env python3
"""把官方 AutoHotkey v2 发行 zip 精简成“原神专用版”。

精简口径（Ji 定，2026-10-30，fork 后精简发行文件）：
  只留：64 位解释器 AutoHotkey64.exe + license.txt + genshin/ 连招与启动器
  删掉：32 位解释器、帮助 chm、UX（Dash / WindowSpy / 安装器脚本）、
        Install.cmd、编译器等一切不在精简包里的东西。
  注意：删的是“发行包里的文件”，不是 fork 仓库里的源码。
  仓库仍保留上游源码（source/ 等），原因有二：
    1. GPLv2：发行二进制时，源码仍在仓库里可查；
    2. 上游同步：删源码后 fork 就没法再合上游更新了。

用法（在 fork 仓库根目录）：
  python3 tools/package-slim.py \
      /tmp/AutoHotkey_2.0.31.zip \
      /tmp/GenshinCombo-slim.zip
  或不传参数，默认读 /tmp/AutoHotkey_2.0.31.zip、写到仓库外
  ../GenshinCombo-slim.zip（避免把产物 zip 本身打进包里）。

产物结构（解压即用，不用另装 AHK，不用 bat）：
  GenshinCombo/
    GenshinLauncher.exe   C 语言启动器：双击即用（须先在 GitHub 上构建）
    bin/AutoHotkey64.exe
    license.txt
    README.txt
    genshin/engine.ahk     连招引擎
    genshin/combos/*.combo
    genshin/docs/ABANDONED.md
    genshin/SOURCES.md
"""
import os
import shutil
import sys
import tempfile
import zipfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEEP = {"AutoHotkey64.exe", "license.txt"}


def main() -> int:
    src_zip = sys.argv[1] if len(sys.argv) > 1 else "/tmp/AutoHotkey_2.0.31.zip"
    out_zip = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(REPO_ROOT), "GenshinCombo-slim.zip")
    if not os.path.isfile(src_zip):
        print(f"找不到官方发行 zip：{src_zip}", file=sys.stderr)
        print("请先从 https://github.com/AutoHotkey/AutoHotkey/releases 下载对应版本。", file=sys.stderr)
        return 1
    genshin_dir = os.path.join(REPO_ROOT, "genshin")
    if not os.path.isdir(genshin_dir):
        print(f"找不到 genshin/ 目录：{genshin_dir}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        stage = os.path.join(tmp, "GenshinCombo")
        os.makedirs(os.path.join(stage, "bin"))
        with zipfile.ZipFile(src_zip) as zf:
            names = zf.namelist()
            for keep in sorted(KEEP):
                if keep not in names:
                    print(f"官方 zip 里没有 {keep}，版本可能变了，先核对再打包。", file=sys.stderr)
                    return 1
            zf.extract("AutoHotkey64.exe", os.path.join(stage, "bin"))
            zf.extract("license.txt", stage)
        # 解释器只留 64 位：32 位、chm、UX、Install.cmd 一律不进包。
        # 旧 AHK 启动器已由 C 启动器取代，不进包。
        shutil.copytree(genshin_dir, os.path.join(stage, "genshin"),
                        ignore=shutil.ignore_patterns("launcher.ahk"))
        launcher_exe = sys.argv[3] if len(sys.argv) > 3 else os.path.join(REPO_ROOT, "GenshinLauncher.exe")
        if os.path.isfile(launcher_exe):
            shutil.copyfile(launcher_exe, os.path.join(stage, "GenshinLauncher.exe"))
        else:
            print("警告：未找到 GenshinLauncher.exe（须先在 GitHub 上构建），包里暂无启动器 exe。")
        with open(os.path.join(stage, "README.txt"), "w", encoding="utf-8") as f:
            f.write("原神连招精简版（AutoHotkey v2 精简发行 + 连招）\n\n")
            f.write("用法：双击 GenshinLauncher.exe，在启动器里看脚本列表、改热键，\n")
            f.write("点“开始连招”后切到原神窗口按快捷键触发连招。\n")
            f.write("全局键：F12 急停，Home 暂停/恢复，End 退出引擎。\n")
            f.write("连招只在原神窗口聚焦时生效；改连招只改 genshin/combos/*.combo。\n\n")
            f.write("本包只含 64 位解释器 + license + 连招，已删 32 位、帮助文档、\n")
            f.write("Dash / WindowSpy / 安装器等文件。AutoHotkey 本体是 GPLv2 开源，\n")
            f.write("源码见 fork 仓库上游部分；连招来源见 genshin/SOURCES.md。\n")
            f.write("注意：本包未在真机 Windows 上跑过，首跑先只开 1 套低风险连招验证。\n")
        if os.path.exists(out_zip):
            os.remove(out_zip)
        with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, _dirs, files in os.walk(stage):
                for name in sorted(files):
                    full = os.path.join(root, name)
                    arc = os.path.relpath(full, tmp)
                    zf.write(full, arc)
        with zipfile.ZipFile(out_zip) as zf:
            bad = [n for n in zf.namelist()
                   if "UX/" in n or n.endswith(".chm") or "AutoHotkey32" in n
                   or n.endswith("Install.cmd")]
            if bad:
                print(f"校验失败，精简包里混进了不该有的文件：{bad}", file=sys.stderr)
                return 1
            print(f"已生成：{out_zip}（{os.path.getsize(out_zip)} 字节）")
            for info in zf.infolist():
                print(f"  {info.file_size:>9}  {info.filename}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
