; ============================================================
; 原神连招启动器 - 简单版（Ji 定，2026-10-30）
; 只管两件事：脚本列表 + 热键修改，别的都不做。
; 用法：由精简包里的解释器运行本文件，或已装 AHK v2 时双击本文件。
;   精简包：启动.bat -> bin\AutoHotkey64.exe genshin\launcher.ahk
;   启动器只负责启动 / 停止 / 重载 engine.ahk，真正打连招的是引擎。
;   关闭本窗口不停止引擎；引擎在托盘继续跑，停止请点“停止引擎”
;   或在原神外按 End（引擎全局键）。
; 注意：本机无 Windows 环境，AHK 语法未在真机上跑过；首跑先只开
;   1 套连招验证，确认没问题再全量启用。
; ============================================================
#Requires AutoHotkey v2.0
#SingleInstance Force

global EnginePID := 0
global ComboFiles := []
global LV := ""
global StatusText := ""

; ---------- 管理员提权（只试一次） ----------
; 引擎由本启动器拉起；启动器已提权时，引擎不再重复提权。
; 注意：这里显式用 A_AhkPath 拉解释器，而不是直接 RunAs 脚本文件，
; 因为精简包用户可能根本没装 AHK、没有 .ahk 文件关联。
Elevated := false
for arg in A_Args {
    if (arg = "/elevated")
        Elevated := true
}
if (!A_IsAdmin && !Elevated) {
    try {
        Run('*RunAs "' . A_AhkPath . '" "' . A_ScriptFullPath . '" /elevated')
        ExitApp()
    } catch {
        MsgBox("未能以管理员身份运行。`n若连招发不进游戏，请右键启动方式选择“以管理员身份运行”。", "原神连招启动器", "OK Icon!")
    }
}

; ---------- 界面：一个列表 + 几颗按钮，仅此而已 ----------
Gui := Gui(, "原神连招启动器 - 脚本与热键管理")
LV := Gui.Add("ListView", "w640 h300 -Multi", ["角色", "连招", "触发键", "模式", "状态"])
LoadList()
StatusText := Gui.Add("Text", "xm w640", "引擎状态：未启动")
BtnStart := Gui.Add("Button", "xm w100", "启动引擎")
BtnStart.OnEvent("Click", (*) => StartEngine())
BtnStop := Gui.Add("Button", "x+10 yp w100", "停止引擎")
BtnStop.OnEvent("Click", (*) => StopEngine())
BtnReload := Gui.Add("Button", "x+10 yp w100", "重载引擎")
BtnReload.OnEvent("Click", (*) => ReloadEngine())
BtnHotkey := Gui.Add("Button", "xm w100", "改热键")
BtnHotkey.OnEvent("Click", (*) => ChangeHotkey())
BtnFolder := Gui.Add("Button", "x+10 yp w140", "打开连招文件夹")
BtnFolder.OnEvent("Click", (*) => OpenComboFolder())
BtnExit := Gui.Add("Button", "x+10 yp w100", "退出启动器")
BtnExit.OnEvent("Click", (*) => ExitApp())
Gui.Add("Text", "xm w640", "触发键是 AHK 写法：^=Ctrl，+=Shift，!=Alt，例如 ^1 = Ctrl+1，none = 停用本套。")
Gui.Add("Text", "xm w640", "改热键后若引擎在运行，会自动重载引擎生效；关闭本窗口不停止引擎。")
Gui.OnEvent("Close", (*) => ExitApp())
Gui.Show("w660 h500")

; ---------- 列表 ----------
LoadList() {
    global LV, ComboFiles
    LV.Delete()
    ComboFiles := []
    Loop Files, A_ScriptDir . "\combos\*.combo" {
        meta := ParseMeta(A_LoopFileFullPath)
        if (meta = "")
            continue
        ComboFiles.Push(A_LoopFileFullPath)
        trig := meta["trigger"]
        if (trig = "none")
            trig := "未绑定"
        LV.Add("", meta["character"], meta["name"], trig, meta["mode"], meta["status"])
    }
    LV.ModifyCol(1, 90)
    LV.ModifyCol(2, 210)
    LV.ModifyCol(3, 110)
    LV.ModifyCol(4, 70)
    LV.ModifyCol(5, 120)
}

; 只读 .combo 文件头的 @ 元数据，不跑步骤。
ParseMeta(path) {
    text := FileRead(path, "UTF-8")
    if (text = "")
        return ""
    meta := Map("character", "", "name", "", "trigger", "none", "mode", "once", "status", "")
    for line in StrSplit(text, "`n", "`r") {
        line := Trim(line)
        if (SubStr(line, 1, 1) != "@")
            continue
        parts := StrSplit(SubStr(line, 2), " ", " `t", 2)
        if (parts.Length >= 2)
            meta[parts[1]] := Trim(SubStr(line, StrLen(parts[1]) + 3))
    }
    if (meta["character"] = "" || meta["name"] = "")
        return ""
    return meta
}

; ---------- 引擎启停 ----------
; 引擎由启动器用 A_AhkPath 拉起：精简包里就是 bin\AutoHotkey64.exe，
; 已装 AHK 的机器上就是已装的解释器，不用猜路径。
; 停止只按 PID 关，不按进程名关——启动器自己也是同一个解释器进程名，
; 按名关会把自己一起关掉。
StartEngine() {
    global EnginePID, StatusText
    if (EnginePID != 0 && ProcessExist(EnginePID)) {
        StatusText.Text := "引擎状态：已在运行（PID " . EnginePID . "）"
        return
    }
    try {
        Run('"' . A_AhkPath . '" "' . A_ScriptDir . '\engine.ahk"', , , &pid)
        EnginePID := pid
        StatusText.Text := "引擎状态：运行中（PID " . pid . "）"
    } catch as err {
        StatusText.Text := "引擎状态：启动失败"
        MsgBox("启动引擎失败：" . err.Message, "原神连招启动器", "OK Icon!")
    }
}

StopEngine() {
    global EnginePID, StatusText
    if (EnginePID = 0 || !ProcessExist(EnginePID)) {
        EnginePID := 0
        StatusText.Text := "引擎状态：未启动（若引擎是启动器重启前拉起的，请用托盘右键退出或在原神外按 End）"
        return
    }
    try {
        ProcessClose(EnginePID)
    } catch as err {
        MsgBox("停止引擎失败：" . err.Message, "原神连招启动器", "OK Icon!")
        return
    }
    EnginePID := 0
    StatusText.Text := "引擎状态：已停止"
}

ReloadEngine() {
    global EnginePID
    if (EnginePID != 0 && ProcessExist(EnginePID)) {
        try {
            ProcessClose(EnginePID)
        }
        EnginePID := 0
        Sleep(400)
    }
    StartEngine()
}

; ---------- 改热键 ----------
ChangeHotkey() {
    global LV, ComboFiles, EnginePID, StatusText
    row := LV.GetNext(0)
    if (row = 0) {
        MsgBox("请先在列表里选一套连招。", "原神连招启动器", "OK Icon!")
        return
    }
    file := ComboFiles[row]
    meta := ParseMeta(file)
    current := meta["trigger"]
    IB := InputBox("输入新触发键（AHK 写法）：`n^1 = Ctrl+1，^+1 = Ctrl+Shift+1`n输入 none = 停用本套", "改热键", "w320 h150", current)
    if (IB.Result != "OK")
        return
    newTrig := Trim(IB.Value)
    if (newTrig != "none" && !RegExMatch(newTrig, "^[\^\+!#]*[0-9a-zA-Z]$")) {
        MsgBox("触发键格式不对，只支持单个字母/数字加 Ctrl/Shift/Alt/Win 修饰，例如 ^1、^+1、^!2。", "原神连招启动器", "OK Icon!")
        return
    }
    for i, f in ComboFiles {
        if (i = row)
            continue
        m := ParseMeta(f)
        if (newTrig != "none" && m["trigger"] = newTrig) {
            MsgBox("触发键 " . newTrig . " 已被 " . m["character"] . " / " . m["name"] . " 占用，换一个。", "原神连招启动器", "OK Icon!")
            return
        }
    }
    text := FileRead(file, "UTF-8")
    newLines := []
    for line in StrSplit(text, "`n", "`r") {
        if (SubStr(Trim(line), 1, 8) = "@trigger")
            newLines.Push("@trigger " . newTrig)
        else
            newLines.Push(line)
    }
    out := ""
    for l in newLines
        out .= l . "`n"
    try {
        FileDelete(file)
        FileAppend(out, file, "UTF-8")
    } catch as err {
        MsgBox("写回连招文件失败：" . err.Message, "原神连招启动器", "OK Icon!")
        return
    }
    LoadList()
    LV.Modify(row, "Select")
    if (EnginePID != 0 && ProcessExist(EnginePID)) {
        ReloadEngine()
        StatusText.Text := "热键已改为 " . newTrig . "，引擎已重载生效。"
    } else {
        StatusText.Text := "热键已改为 " . newTrig . "，引擎未运行，下次启动生效。"
    }
}

OpenComboFolder() {
    try {
        Run(A_ScriptDir . "\combos")
    } catch as err {
        MsgBox("打开连招文件夹失败：" . err.Message, "原神连招启动器", "OK Icon!")
    }
}
