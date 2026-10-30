# 原神连招小助手 - Tauri 版启动器

C 版（`genshin-launcher/launcher.c`，Win32 GDI）能跑，但做漂亮界面
成本太高：圆角、阴影、毛玻璃、滚动、裁剪全要手写，筛选条被卡片盖住
那种 bug 就是手写绘制的典型代价。Tauri 版用 Rust 后端 + HTML/CSS/JS
前端重写，功能跟 C 版完全一致，只是界面层换了：

- 标题、筛选条、底栏固定，**列表是唯一滚动区**，滚动条只在列表区里，
  不会伸进筛选条、标题和底栏（Ji 2026-10-30 明确要求；C 版窗口滚动条
  天生就是整窗高的，分不开，这也是换 Tauri 的原因之一）。
- 筛选条：全部 / ★收藏 / 已绑定 + 角色下拉 + 名字搜索 + 清除。
- 收藏：卡片右上角 ★，存在 `genshin/favorites.txt`，跟连招放一起。
- 改快捷键是页面内弹窗，不是新窗口，不会再出现白底对话框。
- 关闭窗口时引擎一起停，不留后台进程；改快捷键时正在运行就先停、
  改完再起（跟 C 版同一口径）。
- 键盘：↑↓选卡并平滑滚到可见，`/` 聚焦搜索，Enter 改快捷键。

## 构建（GitHub 上构建）

见 `.github/workflows/build-tauri.yml`：windows-latest 装 Rust +
Tauri CLI，`cargo tauri build --no-bundle`，然后按精简口径打包：
Tauri exe 改名 `GenshinLauncher.exe` + 官方 AHK 2.0.31 的 64 位解释器
+ `genshin/`（引擎 + 38 套连招），不含 32 位/chm/UX/旧 C 启动器。

`--no-bundle` 是故意的：不要 Tauri 的安装包，只要一个便携文件夹，
exe 旁边放 `bin/` 和 `genshin/`，文件夹拷走就能用，跟以前的
精简包口径一致。前端文件在构建时嵌进 exe，运行时不需要另外的
网页服务器；WebView2 是 Windows 10/11 自带的，不用另外装。

## 本地开发（可选，Windows 上）

```
cd genshin-tauri/src-tauri
cargo tauri dev
```

本机 Linux VM 没有 WebView2，本地只能改前端样式和 Rust 逻辑，
真机验证以 GitHub 构建 + Ji 的 Windows 为准。
