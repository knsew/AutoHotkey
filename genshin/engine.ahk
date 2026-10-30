; ============================================================
; 原神连招管理器 - 数据驱动引擎 v0.1.0
; AutoHotkey v2.0 only. 不要用 AHK v1 运行本文件。
; 设计口径（Ji 定，2026-10-30）：
;   - 只按 60 帧做；60 帧做不了的连招首期放弃，见 docs/ABANDONED.md
;   - 释放快捷键只用键盘组合，不用鼠标侧键；首期必备套用 Ctrl+数字
;   - 连招数据与引擎分离：改连招只改 combos/*.combo，不改本文件
;   - 只在原神窗口聚焦时执行；F12 急停，Home 暂停/恢复，End 退出
;   - 本机无 Windows 环境，AHK 语法未在真机上跑过；Ji 在 Windows 上首跑时
;     先只开 1 套连招验证，再全量启用
; ============================================================
#Requires AutoHotkey v2.0
#SingleInstance Force

global Combos := []
global StopFlag := false
global Suspended := false
global LoadErrors := []

; ---------- 管理员权限：游戏若以高权限运行，SendInput 需要管理员 ----------
; 只尝试提权一次，避免提权失败时无限重启。
Elevated := false
for arg in A_Args {
    if (arg = "/elevated")
        Elevated := true
}
if (!A_IsAdmin && !Elevated) {
    try {
        Run('*RunAs "' . A_ScriptFullPath . '" /elevated')
        ExitApp()
    } catch {
        ; 提权被拒时继续运行，但要提示：此时按键可能发不进游戏。
        MsgBox("未能以管理员身份运行。`n若连招发不进游戏，请右键 engine.ahk 选择“以管理员身份运行”。", "原神连招管理器", "OK Icon!")
    }
}

; ---------- 托盘菜单 ----------
A_TrayMenu.Delete()
A_TrayMenu.Add("重新加载连招", (*) => ReloadCombos())
A_TrayMenu.Add("暂停 / 恢复（Home）", (*) => ToggleSuspend())
A_TrayMenu.Add("急停当前连招（F12）", (*) => RequestStop())
A_TrayMenu.Add()
A_TrayMenu.Add("退出（End）", (*) => ExitApp())

LoadCombos()
ShowTip("原神连招管理器已启动：已加载 " . Combos.Length . " 套，见托盘。")

; ---------- 全局键：不限制窗口，因为急停/暂停/退出本身就是救命键 ----------
#HotIf
Hotkey("F12", (*) => RequestStop(), "On")
Hotkey("Home", (*) => ToggleSuspend(), "On")
Hotkey("End", (*) => ExitApp(), "On")

; ---------- 连招热键：只在原神窗口聚焦时生效 ----------
; 再加一层 RunCombo 内的聚焦检查，双保险。
#HotIf WinActive("ahk_class UnityWndClass") || WinActive("ahk_exe GenshinImpact.exe") || WinActive("ahk_exe YuanShen.exe")
for idx, combo in Combos {
    if (combo["trigger"] = "" || combo["trigger"] = "none")
        continue
    try {
        Hotkey(combo["trigger"], MakeComboCallback(idx), "On")
    } catch as err {
        LoadErrors.Push(combo["character"] . " / " . combo["name"] . " 注册失败：" . err.Message)
    }
}
#HotIf

if (LoadErrors.Length > 0) {
    msg := "以下连招加载有问题（其余连招仍可用）：`n"
    for e in LoadErrors
        msg .= "`n- " . e
    MsgBox(msg, "原神连招管理器 - 加载警告", "OK Icon!")
    AppendLog(msg)
}

; ============================================================
; 函数
; ============================================================

MakeComboCallback(idx) {
    return (*) => RunCombo(idx)
}

LoadCombos() {
    global Combos, LoadErrors
    Combos := []
    LoadErrors := []
    seenTrigger := Map()
    Loop Files, A_ScriptDir . "\combos\*.combo" {
        combo := ParseComboFile(A_LoopFileFullPath)
        if (combo = "") {
            LoadErrors.Push(A_LoopFileName . "：解析失败，已跳过")
            continue
        }
        trig := combo["trigger"]
        if (trig != "" && trig != "none") {
            if (seenTrigger.Has(trig)) {
                LoadErrors.Push(combo["character"] . " / " . combo["name"] . "：触发键 " . trig . " 与 " . seenTrigger[trig] . " 冲突，已禁用本套")
                combo["trigger"] := "none"
            } else {
                seenTrigger[trig] := combo["character"] . " / " . combo["name"]
            }
        }
        Combos.Push(combo)
    }
    AppendLog("加载连招 " . Combos.Length . " 套，错误 " . LoadErrors.Length . " 条")
}

ReloadCombos() {
    ; 注意：AHK v2 的 Hotkey() 无法直接“反注册”旧回调后再按新数据注册而不重启，
    ; 因此重新加载 = 重启本脚本，简单且不会残留旧热键。
    Reload()
}

ToggleSuspend() {
    global Suspended
    Suspended := !Suspended
    if (Suspended) {
        RequestStop()
        ShowTip("连招管理器已暂停（再按 Home 恢复）")
    } else {
        ShowTip("连招管理器已恢复")
    }
}

RequestStop() {
    global StopFlag
    StopFlag := true
    ReleaseAllKeys()
}

; 连招文件格式（.combo，纯文本，一行一条）：
;   @character 玛薇卡
;   @name 超载全套
;   @trigger ^1          ; AHK 热键写法：^=Ctrl，+=Shift，!=Alt；none = 只入库不绑定
;   @mode once           ; once = 按一次跑完；hold = 按住触发键期间循环跑
;   @status converted    ; converted = 原脚本 1:1 展开；estimate = 按公开手法新写，时序待调；draft = 草稿
;   @source mmmaeko/genshin-ahk Mavu Overload.ahk
;   @fps 60
;   步骤（见 RunSteps 注释）：
;     wait 200            ; 等 200ms
;     wait 70 80          ; 等 70~80ms 随机
;     tap e 50            ; 按 e，按住 50ms 后松开
;     down LButton        ; 按住左键（不松）
;     up LButton          ; 松开左键
;     tap LButton 15      ; 左键点一下，按住 15ms
;     tap RButton 50      ; 右键点一下（原神里常作冲刺取消）
;     move 1000 0         ; 鼠标相对移动（那维莱特转圈类，首期未用）
;     loop 4 / endloop    ; 其间步骤重复 4 次（单层，不嵌套）
ParseComboFile(path) {
    text := FileRead(path)
    if (text = "")
        return ""
    combo := Map("character", "", "name", "", "trigger", "none", "mode", "once",
        "status", "", "source", "", "fps", "60", "steps", [])
    steps := combo["steps"]
    for line in StrSplit(text, "`n", "`r") {
        line := Trim(line)
        if (line = "" || SubStr(line, 1, 1) = ";")
            continue
        if (SubStr(line, 1, 1) = "@") {
            parts := StrSplit(SubStr(line, 2), " ", " `t", 2)
            if (parts.Length >= 2)
                combo[parts[1]] := Trim(SubStr(line, StrLen(parts[1]) + 3))
            continue
        }
        steps.Push(line)
    }
    if (combo["character"] = "" || combo["name"] = "" || steps.Length = 0)
        return ""
    return combo
}

RunCombo(idx) {
    global Combos, StopFlag, Suspended
    if (Suspended)
        return
    if (!IsGenshinActive())
        return
    combo := Combos[idx]
    StopFlag := false
    if (combo["mode"] = "hold") {
        ; 按住触发键期间循环；松开即停。触发键本身是 Ctrl+数字，
        ; 因此要同时检查 Ctrl 和数字键都还按着。
        while (TriggerHeld(combo["trigger"])) {
            if (StopFlag)
                break
            if (!RunSteps(combo, idx))
                break
        }
    } else {
        RunSteps(combo, idx)
    }
    ReleaseAllKeys()
    StopFlag := false
}

; 返回 true = 跑完，false = 中途被打断
RunSteps(combo, idx) {
    global StopFlag
    steps := combo["steps"]
    i := 1
    loopStack := []
    while (i <= steps.Length) {
        if (StopFlag)
            return false
        if (combo["mode"] = "hold" && !TriggerHeld(combo["trigger"]))
            return false
        line := steps[i]
        parts := StrSplit(line, " ", " `t")
        cmd := parts[1]
        if (cmd = "loop") {
            n := Integer(parts[2])
            loopStack.Push({start: i, left: n})
            i += 1
            continue
        }
        if (cmd = "endloop") {
            if (loopStack.Length = 0) {
                i += 1
                continue
            }
            top := loopStack[loopStack.Length]
            top["left"] -= 1
            if (top["left"] > 0) {
                i := top["start"] + 1
            } else {
                loopStack.Pop()
                i += 1
            }
            continue
        }
        if (!RunStep(line, combo)) {
            return false
        }
        i += 1
    }
    return true
}

; 返回 true = 继续，false = 被打断
RunStep(line, combo) {
    global StopFlag
    parts := StrSplit(line, " ", " `t")
    cmd := parts[1]
    if (cmd = "wait") {
        if (parts.Length >= 3)
            ms := Random(Integer(parts[2]), Integer(parts[3]))
        else
            ms := Integer(parts[2])
        return SleepInterrupt(ms, combo)
    }
    if (cmd = "tap") {
        key := MapKey(parts[2])
        hold := (parts.Length >= 3) ? Integer(parts[3]) : 50
        Send("{" . key . " Down}")
        if (!SleepInterrupt(hold, combo)) {
            Send("{" . key . " Up}")
            return false
        }
        Send("{" . key . " Up}")
        return true
    }
    if (cmd = "down") {
        Send("{" . MapKey(parts[2]) . " Down}")
        return true
    }
    if (cmd = "up") {
        Send("{" . MapKey(parts[2]) . " Up}")
        return true
    }
    if (cmd = "move") {
        DllCall("mouse_event", "UInt", 0x0001, "Int", Integer(parts[2]), "Int", Integer(parts[3]), "UInt", 0, "UPtr", 0)
        return true
    }
    ; 未知步骤：不静默跳过，记日志并中断本套，避免跑出一串错的按键。
    AppendLog("未知步骤，已中断：" . line)
    return false
}

; 分段睡，每 10ms 检查一次急停 / hold 松开，避免 Sleep 一睡几秒停不下来。
SleepInterrupt(ms, combo) {
    global StopFlag
    left := ms
    while (left > 0) {
        if (StopFlag)
            return false
        if (combo["mode"] = "hold" && !TriggerHeld(combo["trigger"]))
            return false
        chunk := Min(left, 10)
        Sleep(chunk)
        left -= chunk
    }
    return true
}

TriggerHeld(trigger) {
    needCtrl := InStr(trigger, "^") > 0
    needShift := InStr(trigger, "+") > 0
    needAlt := InStr(trigger, "!") > 0
    main := trigger
    main := StrReplace(main, "^", "")
    main := StrReplace(main, "+", "")
    main := StrReplace(main, "!", "")
    main := StrReplace(main, "#", "")
    main := StrReplace(main, "$", "")
    main := StrReplace(main, "~", "")
    if (needCtrl && !GetKeyState("Ctrl", "P"))
        return false
    if (needShift && !GetKeyState("Shift", "P"))
        return false
    if (needAlt && !GetKeyState("Alt", "P"))
        return false
    if (main != "" && !GetKeyState(main, "P"))
        return false
    return true
}

MapKey(k) {
    switch k {
        case "click": return "LButton"
        case "rclick": return "RButton"
        case "space": return "Space"
        case "shift": return "Shift"
        default: return k
    }
}

ReleaseAllKeys() {
    Send("{LButton Up}")
    Send("{RButton Up}")
    Send("{Shift Up}")
    Send("{Space Up}")
    Send("{e Up}")
    Send("{q Up}")
    Send("{w Up}")
    Send("{s Up}")
    Send("{a Up}")
    Send("{d Up}")
    Send("{r Up}")
}

IsGenshinActive() {
    return WinActive("ahk_class UnityWndClass") || WinActive("ahk_exe GenshinImpact.exe") || WinActive("ahk_exe YuanShen.exe")
}

ShowTip(text) {
    ToolTip(text, 0, 0)
    SetTimer(ClearTip, -1500)
}

ClearTip() {
    ToolTip()
}

AppendLog(text) {
    try {
        FileAppend(FormatTime(, "yyyy-MM-dd HH:mm:ss") . " " . text . "`n", A_ScriptDir . "\load.log", "UTF-8")
    }
}
