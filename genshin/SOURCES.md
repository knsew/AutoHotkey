# 来源对照

个人自用整理，原作者与来源保留在每个连招文件头的 `@source` 里。
若以后公开发布，不要把别人的宏原样当成自己的发出去。

## 必备套

- 玛薇卡、丝柯克：mmmaeko/genshin-ahk
  https://github.com/mmmaeko/genshin-ahk
  本地核对提交：f0ac4eabde516f0c4e9b04853288e741505fe3a6（2025-12-28，丝柯克）
  文件：Mavu Overload.ahk、Mavu Melt 2(C3Fd) C1F.ahk、
  Mavu Manual CDCF.ahk、Mavu Manual 2CD CF.ahk、Skirk 2N2D 3NW.ahk
  状态：1:1 展开，函数调用已展开成最小步骤，随机抖动范围保留。
- 魈：Kramar1337/GenshinImpact-AHK-flex
  https://github.com/Kramar1337/GenshinImpact-AHK-flex
  本地核对提交：6a6d1a29f60e4679ea8a118568966ba63e2b66b2（2026-04-28）
  位置：主脚本 jopa15（SpamPlunge）、jopa16（N1SpamPlunge）
  状态：按住循环 1:1 展开；13 / 11 跳只是 README 叫法，代码里不计数。
- 兹白：无 GitHub 现成宏（2026-10-30 搜 zibai macro / ahk / 兹白宏均为 0 结果）。
  依据公开手法记号：全套 E → 4×[N4E]；压缩版 E n4 n4 E n1E n1E QE；
  大招变体把 1 轮 N4E 换成 N1QBurst；4 普攻约 2.4 秒、特殊E 约 0.45 秒。
  状态：新写，时序全部待调；大招变体为草稿。

## 易转库

- NineTailTeam/Genshin-Impact-Macro
  https://github.com/NineTailTeam/Genshin-Impact-Macro
  本地核对提交：65fa9c5a2455161a5b434e5f0812e6b3c13fb356
  原仓库说明：All macros work at 60 FPS, 90 ms Ping；
  Caution: Use of macros may result in a ban.
  生成工具：tools/convert_ninetail.py，28 个 Combat/*.ahk 全量转换，
  无 UNTRANSLATED 残留；刻晴 KeyWait 差异见 README。

## 放弃 / 延后（不在 combos 里）

- 恰斯卡：ShiyunKong/Genshin-Impact-Chasca-Macro-for-C6
  https://github.com/ShiyunKong/Genshin-Impact-Chasca-Macro-for-C6
  放弃原因见 docs/ABANDONED.md（120/144 帧解锁强制要求）。
- FogMoe、AHK-flex 其余部分、BotMek、gcsim：延后，原因见 README 后期批次。
