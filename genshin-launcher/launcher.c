/* ============================================================
 * 原神连招小助手 - C 语言 Win32 启动器（Ji 定，2026-10-30）
 *
 * 给女孩子用的：双击 exe 就能用，不用 bat、不用装 AHK、不用懂脚本。
 * 只做三件事：看脚本列表、改快捷键、开始 / 停止连招。
 * 关闭窗口时，连招也会一起停止，不留一个不知道怎么关的后台进程。
 *
 * 构建（GitHub 上构建，见 .github/workflows/build-launcher.yml）：
 *   cl /utf-8 /O2 /DUNICODE /D_UNICODE genshin-launcher/launcher.c
 *      /link /MANIFEST:embed /MANIFESTINPUT:genshin-launcher/launcher.manifest
 *      user32.lib gdi32.lib shell32.lib
 * 注意：源码是 UTF-8，/utf-8 必不可少，否则中文界面会乱码。
 * 本机无 Windows 环境，本文件未在真机上编译运行过，首跑以 CI 为准。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

#define HEADER_H 120
#define FOOTER_H 112
#define CARD_H 84
#define CARD_GAP 16
#define MAX_COMBOS 256

#define ID_BTN_START 101
#define ID_BTN_STOP 102
#define ID_BTN_EDIT 103
#define ID_BTN_FOLDER 104
#define ID_DLG_OK 201
#define ID_DLG_CANCEL 202
#define ID_DLG_DISABLE 203
#define ID_TIMER_ENGINE 1

typedef struct {
    wchar_t filePath[MAX_PATH];
    wchar_t character[64];
    wchar_t name[128];
    char trigger[32];
    char mode[16];
    char status[32];
} Combo;

static HINSTANCE g_hInst;
static HWND g_hMain, g_hBtnStart, g_hBtnStop, g_hBtnEdit, g_hBtnFolder;
static HWND g_hDlg, g_hDlgEdit;
static int g_editIndex = -1;
static HFONT g_fontTitle, g_fontSub, g_fontCard, g_fontCardSmall, g_fontPill, g_fontBtn;
static Combo g_combos[MAX_COMBOS];
static Combo g_sortTmp[MAX_COMBOS];
static int g_count = 0;
static int g_scroll = 0;
static int g_selected = -1;
static int g_engineRunning = 0;
static HANDLE g_hEngine = NULL;
static wchar_t g_exeDir[MAX_PATH];
static wchar_t g_interpPath[MAX_PATH];
static wchar_t g_enginePath[MAX_PATH];
static wchar_t g_comboDir[MAX_PATH];

/* ---------------- 小工具 ---------------- */

static void TrimChar(char *s) {
    char *p = s;
    size_t n;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

static void Utf8ToWide(const char *src, int srcLen, wchar_t *dst, int dstChars) {
    int w;
    if (dstChars <= 0) return;
    if (srcLen < 0) srcLen = (int)strlen(src);
    w = MultiByteToWideChar(CP_UTF8, 0, src, srcLen, dst, dstChars - 1);
    if (w < 0) w = 0;
    dst[w] = L'\0';
}

static void BuildPaths(void) {
    wchar_t *slash;
    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash) *slash = L'\0';
    wcscpy(g_interpPath, g_exeDir);
    wcscat(g_interpPath, L"\\bin\\AutoHotkey64.exe");
    wcscpy(g_enginePath, g_exeDir);
    wcscat(g_enginePath, L"\\genshin\\engine.ahk");
    wcscpy(g_comboDir, g_exeDir);
    wcscat(g_comboDir, L"\\genshin\\combos");
}

/* AHK 写法 -> 好看的写法：^1 -> Ctrl+1，^+1 -> Ctrl+Shift+1，none -> 未绑定 */
static void DisplayTrigger(const char *trig, wchar_t *out, int outChars) {
    int i, o = 0;
    if (strcmp(trig, "none") == 0) {
        wcscpy(out, L"未绑定");
        return;
    }
    out[0] = L'\0';
    for (i = 0; trig[i] != '\0' && o < outChars - 8; i++) {
        const wchar_t *word = NULL;
        if (trig[i] == '^') word = L"Ctrl+";
        else if (trig[i] == '+') word = L"Shift+";
        else if (trig[i] == '!') word = L"Alt+";
        else if (trig[i] == '#') word = L"Win+";
        else {
            out[o++] = (wchar_t)(unsigned char)trig[i];
            out[o] = L'\0';
            continue;
        }
        wcscpy(out + o, word);
        o += (int)wcslen(word);
    }
    /* 去掉末尾多余的 +（正常输入不会有，但防一手） */
    if (o > 0 && out[o - 1] == L'+') out[o - 1] = L'\0';
}

/* ---------------- 连招文件 ---------------- */

/* 只读文件头的 @ 元数据。返回 1 = 有效，0 = 跳过。 */
static int ParseComboFile(const wchar_t *path, Combo *out) {
    FILE *f;
    long size;
    char *buf, *p;
    char line[1024];
    memset(out, 0, sizeof(*out));
    wcscpy(out->filePath, path);
    strcpy(out->trigger, "none");
    strcpy(out->mode, "once");
    f = _wfopen(path, L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1024 * 1024) { fclose(f); return 0; }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    buf[size] = '\0';
    p = buf;
    /* 跳过 UTF-8 BOM */
    if (size >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        p = buf + 3;
    while (*p) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char *sp, *val;
        char key[32];
        size_t klen;
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = e ? e + 1 : p + len;
        TrimChar(line);
        if (line[0] == '\0' || line[0] == ';' || line[0] != '@') continue;
        sp = line + 1;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        klen = (size_t)(sp - (line + 1));
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        memcpy(key, line + 1, klen);
        key[klen] = '\0';
        while (*sp == ' ' || *sp == '\t') sp++;
        val = sp;
        TrimChar(val);
        if (strcmp(key, "character") == 0) Utf8ToWide(val, -1, out->character, 64);
        else if (strcmp(key, "name") == 0) Utf8ToWide(val, -1, out->name, 128);
        else if (strcmp(key, "trigger") == 0) { strncpy(out->trigger, val, 31); out->trigger[31] = '\0'; }
        else if (strcmp(key, "mode") == 0) { strncpy(out->mode, val, 15); out->mode[15] = '\0'; }
        else if (strcmp(key, "status") == 0) { strncpy(out->status, val, 31); out->status[31] = '\0'; }
    }
    free(buf);
    return (out->character[0] != L'\0' && out->name[0] != L'\0');
}

static void LoadCombos(void) {
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int i, n = 0;
    wcscpy(pattern, g_comboDir);
    wcscat(pattern, L"\\*.combo");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { g_count = 0; g_selected = -1; return; }
    do {
        wchar_t full[MAX_PATH];
        Combo tmp;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wcscpy(full, g_comboDir);
        wcscat(full, L"\\");
        wcscat(full, fd.cFileName);
        if (ParseComboFile(full, &tmp) && n < MAX_COMBOS)
            g_combos[n++] = tmp;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    /* 已绑定的排前面，没绑定的排后面，各自保持文件顺序，好找常用套。 */
    {
        int m = 0;
        for (i = 0; i < n; i++)
            if (strcmp(g_combos[i].trigger, "none") != 0) g_sortTmp[m++] = g_combos[i];
        for (i = 0; i < n; i++)
            if (strcmp(g_combos[i].trigger, "none") == 0) g_sortTmp[m++] = g_combos[i];
        for (i = 0; i < n; i++) g_combos[i] = g_sortTmp[i];
    }
    g_count = n;
    if (g_selected >= n) g_selected = -1;
}

/* 把某套的 @trigger 整行换掉，其余行原样保留。返回 1 = 成功。 */
static int RewriteTrigger(const wchar_t *path, const char *newTrig) {
    FILE *f;
    long size;
    char *buf, *p, *out, *o;
    size_t cap;
    int hasBom = 0, ok = 0;
    f = _wfopen(path, L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1024 * 1024) { fclose(f); return 0; }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    buf[size] = '\0';
    if (size >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        hasBom = 1;
    cap = (size_t)size + 64;
    out = (char *)malloc(cap);
    if (!out) { free(buf); return 0; }
    o = out;
    if (hasBom) { memcpy(o, buf, 3); o += 3; }
    p = buf + (hasBom ? 3 : 0);
    while (*p) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[1024];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = e ? e + 1 : p + len;
        TrimChar(line);
        if (strncmp(line, "@trigger", 8) == 0 && (line[8] == ' ' || line[8] == '\t' || line[8] == '\0')) {
            o += sprintf(o, "@trigger %s\n", newTrig);
        } else {
            o += sprintf(o, "%s\n", line);
        }
    }
    *o = '\0';
    f = _wfopen(path, L"wb");
    if (f) {
        fwrite(out, 1, strlen(out), f);
        fclose(f);
        ok = 1;
    }
    free(buf);
    free(out);
    return ok;
}

/* ---------------- 引擎启停 ---------------- */
/* 只按 PID 关，不按进程名关：启动器自己也是同一个解释器进程名，
 * 按名关会把自己一起关掉。关闭启动器窗口时，引擎也会一起停。 */

static void UpdateButtons(void) {
    if (g_hBtnStart) EnableWindow(g_hBtnStart, !g_engineRunning);
    if (g_hBtnStop) EnableWindow(g_hBtnStop, g_engineRunning);
    if (g_hBtnEdit) EnableWindow(g_hBtnEdit, g_selected >= 0);
}

static void StartEngine(void) {
    wchar_t cmd[2048];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (g_engineRunning) return;
    if (GetFileAttributesW(g_interpPath) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_hMain, L"找不到解释器 bin\\AutoHotkey64.exe，请确认它在启动器旁边。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
        return;
    }
    if (GetFileAttributesW(g_enginePath) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_hMain, L"找不到连招引擎 genshin\\engine.ahk，请确认文件夹完整。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
        return;
    }
    wcscpy(cmd, L"\"");
    wcscat(cmd, g_interpPath);
    wcscat(cmd, L"\" \"");
    wcscat(cmd, g_enginePath);
    wcscat(cmd, L"\"");
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(g_hMain, L"启动连招失败，请右键启动器选择“以管理员身份运行”再试一次。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
        return;
    }
    CloseHandle(pi.hThread);
    g_hEngine = pi.hProcess;
    g_engineRunning = 1;
    UpdateButtons();
    InvalidateRect(g_hMain, NULL, TRUE);
}

static void StopEngine(void) {
    if (!g_engineRunning || !g_hEngine) { g_engineRunning = 0; return; }
    TerminateProcess(g_hEngine, 0);
    WaitForSingleObject(g_hEngine, 500);
    CloseHandle(g_hEngine);
    g_hEngine = NULL;
    g_engineRunning = 0;
    UpdateButtons();
    InvalidateRect(g_hMain, NULL, TRUE);
}

static void CheckEngineExited(void) {
    if (g_engineRunning && g_hEngine && WaitForSingleObject(g_hEngine, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_hEngine);
        g_hEngine = NULL;
        g_engineRunning = 0;
        UpdateButtons();
        InvalidateRect(g_hMain, NULL, TRUE);
    }
}

/* ---------------- 改快捷键 ---------------- */

/* 好看的输入（Ctrl+1）-> AHK 写法（^1）。返回 1 = 合法。 */
static int ParseFriendlyHotkey(const wchar_t *in, char *out, int outChars) {
    wchar_t buf[64];
    wchar_t *p, *tok;
    int hasCtrl = 0, hasShift = 0, hasAlt = 0, hasWin = 0;
    wchar_t last[16];
    int lastLen = 0, i, o = 0;
    if (outChars <= 0) return 0;
    wcscpy(buf, in);
    /* 去空格 */
    {
        wchar_t *r = buf, *w = buf;
        while (*r) { if (*r != L' ') *w++ = *r; r++; }
        *w = L'\0';
    }
    if (buf[0] == L'\0') return 0;
    /* 直接写 AHK 写法也接受：^1、^+1 */
    if (buf[0] == L'^' || buf[0] == L'+' || buf[0] == L'!' || buf[0] == L'#') {
        for (i = 0; buf[i] && o < outChars - 1; i++) {
            if (buf[i] > 127) return 0;
            out[o++] = (char)buf[i];
        }
        out[o] = '\0';
        return o >= 2;
    }
    last[0] = L'\0';
    p = buf;
    while (1) {
        wchar_t *plus = wcschr(p, L'+');
        if (plus) *plus = L'\0';
        tok = p;
        /* 转小写比较 */
        for (i = 0; tok[i]; i++)
            if (tok[i] >= L'A' && tok[i] <= L'Z') tok[i] = (wchar_t)(tok[i] + 32);
        if (wcscmp(tok, L"ctrl") == 0) hasCtrl = 1;
        else if (wcscmp(tok, L"shift") == 0) hasShift = 1;
        else if (wcscmp(tok, L"alt") == 0) hasAlt = 1;
        else if (wcscmp(tok, L"win") == 0) hasWin = 1;
        else {
            lastLen = (int)wcslen(tok);
            if (lastLen != 1) return 0;
            last[0] = tok[0];
            last[1] = L'\0';
        }
        if (!plus) break;
        p = plus + 1;
    }
    if (lastLen != 1 || last[0] > 127) return 0;
    /* 必须带至少一个修饰键：单按 1、2 这种在原神里本身就是切人键，不能绑 */
    if (!hasCtrl && !hasShift && !hasAlt && !hasWin) return 0;
    if (hasCtrl) out[o++] = '^';
    if (hasShift) out[o++] = '+';
    if (hasAlt) out[o++] = '!';
    if (hasWin) out[o++] = '#';
    if (last[0] >= L'A' && last[0] <= L'Z') last[0] = (wchar_t)(last[0] + 32);
    out[o++] = (char)last[0];
    out[o] = '\0';
    return 1;
}

static void CloseHotkeyDialog(void) {
    if (g_hDlg) {
        HWND d = g_hDlg;
        g_hDlg = NULL;
        EnableWindow(g_hMain, TRUE);
        SetForegroundWindow(g_hMain);
        DestroyWindow(d);
    }
}

static void ApplyHotkey(const char *newTrig) {
    int wasRunning;
    if (g_editIndex < 0 || g_editIndex >= g_count) return;
    if (!RewriteTrigger(g_combos[g_editIndex].filePath, newTrig)) {
        MessageBoxW(g_hDlg ? g_hDlg : g_hMain, L"改快捷键失败，文件写不进去，请检查文件夹是不是只读。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
        return;
    }
    wasRunning = g_engineRunning;
    if (wasRunning) StopEngine();
    LoadCombos();
    g_selected = -1;
    UpdateButtons();
    if (wasRunning) StartEngine();
    InvalidateRect(g_hMain, NULL, TRUE);
    CloseHotkeyDialog();
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        wchar_t init[64];
        HWND hEdit, hOk, hCancel, hDisable;
        /* 标签不再用 STATIC 子控件：上一版靠 WM_CTLCOLORSTATIC 改深色
         * 在真机上没生效（对话框还是白底黑字，见截图），所以直接
         * 在 WM_PAINT 里画文字，对话框背景也直接画深色，不再指望
         * CTLCOLOR 去改静态文本的底色。 */
        hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            20, 72, 360, 30, hwnd, NULL, g_hInst, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_fontCard, TRUE);
        g_hDlgEdit = hEdit;
        if (g_editIndex >= 0 && g_editIndex < g_count) {
            DisplayTrigger(g_combos[g_editIndex].trigger, init, 64);
            if (wcscmp(init, L"未绑定") != 0) SetWindowTextW(hEdit, init);
        }
        hOk = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 20, 122, 100, 36, hwnd, (HMENU)ID_DLG_OK, g_hInst, NULL);
        hCancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 132, 122, 100, 36, hwnd, (HMENU)ID_DLG_CANCEL, g_hInst, NULL);
        hDisable = CreateWindowExW(0, L"BUTTON", L"停用这套", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 244, 122, 100, 36, hwnd, (HMENU)ID_DLG_DISABLE, g_hInst, NULL);
        (void)hOk; (void)hCancel; (void)hDisable;
        SetFocus(hEdit);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HBRUSH brush = CreateSolidBrush(RGB(24, 14, 44));
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        SelectObject(hdc, g_fontCardSmall);
        rc.left = 20; rc.top = 16; rc.right = 380; rc.bottom = 40;
        DrawTextW(hdc, L"输入新的快捷键，例如：Ctrl+1、Ctrl+Shift+1。", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, RGB(190, 180, 210));
        rc.top = 40; rc.bottom = 64;
        DrawTextW(hdc, L"改完点保存，正在运行的连招会自动重载生效。", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        static HBRUSH dlgBrush = NULL;
        if (!dlgBrush) dlgBrush = CreateSolidBrush(RGB(24, 14, 44));
        SetBkMode((HDC)wParam, TRANSPARENT);
        SetTextColor((HDC)wParam, RGB(255, 255, 255));
        SetBkColor((HDC)wParam, RGB(24, 14, 44));
        return (LRESULT)dlgBrush;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lParam;
        HBRUSH brush;
        HPEN pen;
        HGDIOBJ oldBrush, oldPen;
        COLORREF bg, fg;
        int primary = (di->CtlID == ID_DLG_OK);
        if (primary) { bg = RGB(124, 58, 237); fg = RGB(255, 255, 255); }
        else { bg = RGB(45, 35, 75); fg = RGB(255, 255, 255); }
        brush = CreateSolidBrush(bg);
        pen = CreatePen(PS_SOLID, 1, primary ? bg : RGB(90, 70, 140));
        oldBrush = SelectObject(di->hDC, brush);
        oldPen = SelectObject(di->hDC, pen);
        RoundRect(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, 14, 14);
        SelectObject(di->hDC, oldBrush);
        SelectObject(di->hDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);
        SelectObject(di->hDC, g_fontBtn);
        {
            wchar_t text[64];
            GetWindowTextW(di->hwndItem, text, 64);
            DrawTextW(di->hDC, text, -1, &di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_DLG_OK) {
            wchar_t in[64];
            char ahk[32];
            int i;
            GetWindowTextW(g_hDlgEdit, in, 64);
            if (in[0] == L'\0') {
                MessageBoxW(hwnd, L"请先输入快捷键，或点“停用这套”。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (wcscmp(in, L"停用") == 0 || wcscmp(in, L"无") == 0) {
                ApplyHotkey("none");
                return 0;
            }
            if (!ParseFriendlyHotkey(in, ahk, 32)) {
                MessageBoxW(hwnd, L"格式不对哦，只支持 Ctrl / Shift / Alt / Win + 一个字母或数字，例如 Ctrl+1、Ctrl+Shift+1。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
                return 0;
            }
            for (i = 0; i < g_count; i++) {
                if (i != g_editIndex && strcmp(g_combos[i].trigger, ahk) == 0) {
                    wchar_t msg[256];
                    swprintf(msg, 256, L"这个快捷键已经被“%s · %s”占了，换一个吧。", g_combos[i].character, g_combos[i].name);
                    MessageBoxW(hwnd, msg, L"原神连招小助手", MB_OK | MB_ICONWARNING);
                    return 0;
                }
            }
            ApplyHotkey(ahk);
            return 0;
        }
        if (id == ID_DLG_CANCEL) { CloseHotkeyDialog(); return 0; }
        if (id == ID_DLG_DISABLE) { ApplyHotkey("none"); return 0; }
        break;
    }
    case WM_CLOSE:
        CloseHotkeyDialog();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void OpenHotkeyDialog(void) {
    RECT rc;
    int x, y;
    if (g_selected < 0 || g_selected >= g_count) {
        MessageBoxW(g_hMain, L"请先在列表里点一套连招。", L"原神连招小助手", MB_OK | MB_ICONWARNING);
        return;
    }
    if (g_hDlg) { SetForegroundWindow(g_hDlg); return; }
    g_editIndex = g_selected;
    GetWindowRect(g_hMain, &rc);
    x = rc.left + ((rc.right - rc.left) - 400) / 2;
    y = rc.top + ((rc.bottom - rc.top) - 200) / 2;
    g_hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"GenshinHotkeyDialog", L"改快捷键",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 400, 200, g_hMain, NULL, g_hInst, NULL);
    if (!g_hDlg) return;
    EnableWindow(g_hMain, FALSE);
    ShowWindow(g_hDlg, SW_SHOW);
    UpdateWindow(g_hDlg);
}

/* ---------------- 主窗口绘制 ---------------- */

static int ContentHeight(void) {
    return 12 + g_count * (CARD_H + CARD_GAP);
}

static int MaxScroll(int clientH) {
    int vis = clientH - HEADER_H - FOOTER_H;
    int max = ContentHeight() - vis + 12;
    return max > 0 ? max : 0;
}

static void UpdateScroll(int clientH) {
    SCROLLINFO si;
    int max = MaxScroll(clientH);
    if (g_scroll > max) g_scroll = max;
    if (g_scroll < 0) g_scroll = 0;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = ContentHeight();
    si.nPage = (UINT)(clientH - HEADER_H - FOOTER_H);
    si.nPos = g_scroll;
    SetScrollInfo(g_hMain, SB_VERT, &si, TRUE);
}

static void LayoutButtons(int clientW, int clientH) {
    int bw = 128, bh = 48, gap = 12, total = 4 * bw + 3 * gap;
    int x = clientW - 20 - total;
    int y = clientH - FOOTER_H + 52;
    if (g_hBtnStart) MoveWindow(g_hBtnStart, x, y, bw, bh, TRUE);
    if (g_hBtnStop) MoveWindow(g_hBtnStop, x + (bw + gap), y, bw, bh, TRUE);
    if (g_hBtnEdit) MoveWindow(g_hBtnEdit, x + 2 * (bw + gap), y, bw, bh, TRUE);
    if (g_hBtnFolder) MoveWindow(g_hBtnFolder, x + 3 * (bw + gap), y, bw, bh, TRUE);
}


/* 纵向渐变：按 4px 一条带插值填充，GDI 没有现成的渐变 API 就这么画，
 * 卡片和按钮不多，Paint 时这点开销可以忽略。 */
static void FillVerticalGradient(HDC hdc, int x, int y, int w, int h, COLORREF top, COLORREF bottom) {
    int i;
    for (i = 0; i < h; i += 4) {
        int t = h > 1 ? (i * 255) / h : 0;
        COLORREF c = RGB((GetRValue(top) * (255 - t) + GetRValue(bottom) * t) / 255,
                         (GetGValue(top) * (255 - t) + GetGValue(bottom) * t) / 255,
                         (GetBValue(top) * (255 - t) + GetBValue(bottom) * t) / 255);
        HBRUSH b = CreateSolidBrush(c);
        RECT r;
        r.left = x; r.top = y + i; r.right = x + w; r.bottom = y + i + 4;
        if (r.bottom > y + h) r.bottom = y + h;
        FillRect(hdc, &r, b);
        DeleteObject(b);
    }
}

static void FillHorizontalGradient(HDC hdc, int x, int y, int w, int h, COLORREF left, COLORREF right) {
    int i;
    for (i = 0; i < w; i += 4) {
        int t = w > 1 ? (i * 255) / w : 0;
        COLORREF c = RGB((GetRValue(left) * (255 - t) + GetRValue(right) * t) / 255,
                         (GetGValue(left) * (255 - t) + GetGValue(right) * t) / 255,
                         (GetBValue(left) * (255 - t) + GetBValue(right) * t) / 255);
        HBRUSH b = CreateSolidBrush(c);
        RECT r;
        r.left = x + i; r.top = y; r.right = x + i + 4; r.bottom = y + h;
        if (r.right > x + w) r.right = x + w;
        FillRect(hdc, &r, b);
        DeleteObject(b);
    }
}


/* 柔光圆：参考图里的紫色光晕不是实心圆，是边缘渐隐的。
 * 以前用实心 Ellipse 画，边缘一刀切，看起来就廉价。
 * 这里用 32 位 DIB 做径向 alpha 衰减，再 AlphaBlend 贴上去，
 * 边缘是二次方衰减的软光晕。需要链接 msimg32.lib。 */
static void DrawSoftGlow(HDC hdc, int cx, int cy, int radius, COLORREF color, int maxAlpha) {
    int d = radius * 2, x, y;
    BITMAPINFO bi;
    void *bits = NULL;
    HBITMAP hbmp;
    HDC mem;
    HGDIOBJ old;
    BLENDFUNCTION bf;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = d;
    bi.bmiHeader.biHeight = -d;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    hbmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp || !bits) return;
    for (y = 0; y < d; y++) {
        for (x = 0; x < d; x++) {
            double dx = (double)x - radius, dy = (double)y - radius;
            double dist = sqrt(dx * dx + dy * dy) / radius;
            int a = 0;
            unsigned char *px = (unsigned char *)bits + (y * d + x) * 4;
            if (dist < 1.0) {
                double f = 1.0 - dist;
                a = (int)(maxAlpha * f * f);
            }
            px[0] = (unsigned char)(GetBValue(color) * a / 255);
            px[1] = (unsigned char)(GetGValue(color) * a / 255);
            px[2] = (unsigned char)(GetRValue(color) * a / 255);
            px[3] = (unsigned char)a;
        }
    }
    mem = CreateCompatibleDC(hdc);
    old = SelectObject(mem, hbmp);
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(hdc, cx - radius, cy - radius, d, d, mem, 0, 0, d, d, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(hbmp);
}

static void PaintMain(HDC hdc, int clientW, int clientH) {
    RECT rc;
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ oldBrush, oldPen, oldFont;
    int i;
    /* 背景：截图同款深紫渐变（上深下稍亮），顶栏不再是实色条，
     * 标题直接画在渐变上；右上角两个大圆是截图里的装饰圆。 */
    FillVerticalGradient(hdc, 0, 0, clientW, clientH, RGB(24, 13, 48), RGB(36, 22, 68));
    /* 柔光：右上角大光晕 + 中间小光晕，参考图就是这种软光 */
    DrawSoftGlow(hdc, clientW - 20, 10, 240, RGB(139, 92, 246), 170);
    DrawSoftGlow(hdc, clientW - 190, 150, 110, RGB(124, 58, 237), 120);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    oldFont = SelectObject(hdc, g_fontTitle);
    rc.left = 24; rc.top = 18; rc.right = clientW - 24; rc.bottom = 70;
    DrawTextW(hdc, L"原神连招小助手", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, g_fontSub);
    rc.top = 74; rc.bottom = 102;
    SetTextColor(hdc, RGB(210, 200, 230));
    DrawTextW(hdc, L"双击就能用 · 只在原神窗口里生效，聊天时按了也不会跑", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    /* 卡片列表：只在列表区内画，滚上去的卡片不许盖住顶栏和底栏。
     * 这就是之前“翻滚后遮挡”的根因：以前卡片是先画顶栏、后画卡片，
     * 半截卡片会直接盖到顶栏副标题上。现在用裁剪区把它拦住。 */
    {
        HRGN hClip = CreateRectRgn(0, HEADER_H, clientW, clientH - FOOTER_H);
        SelectClipRgn(hdc, hClip);
        DeleteObject(hClip);
    }
    for (i = 0; i < g_count; i++) {
        int cardY = HEADER_H + 12 + i * (CARD_H + CARD_GAP) - g_scroll;
        int cardX = 20, cardW = clientW - 40;
        wchar_t trig[64], modeText[32];
        SIZE sz;
        int pillW, pillX, pillY;
        if (cardY + CARD_H < HEADER_H || cardY > clientH - FOOTER_H) continue;
        {
            HRGN hCardClip = CreateRoundRectRgn(cardX, cardY, cardX + cardW, cardY + CARD_H, 24, 24);
            SelectClipRgn(hdc, hCardClip);
            DeleteObject(hCardClip);
            FillVerticalGradient(hdc, cardX, cardY, cardW, CARD_H,
                i == g_selected ? RGB(62, 46, 102) : RGB(51, 38, 90),
                i == g_selected ? RGB(44, 32, 74) : RGB(36, 26, 61));
            SelectClipRgn(hdc, NULL);
        }
        pen = CreatePen(PS_SOLID, i == g_selected ? 2 : 1, i == g_selected ? RGB(168, 85, 247) : RGB(122, 106, 168));
        oldPen = SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, cardX, cardY, cardX + cardW, cardY + CARD_H, 24, 24);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        /* 顶部高光线：卡片看起来有玻璃感就靠这一条 */
        pen = CreatePen(PS_SOLID, 1, RGB(150, 130, 200));
        oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, cardX + 24, cardY + 1, NULL);
        LineTo(hdc, cardX + cardW - 24, cardY + 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        oldFont = SelectObject(hdc, g_fontCard);
        rc.left = cardX + 24; rc.top = cardY + 14; rc.right = cardX + 150; rc.bottom = cardY + 44;
        DrawTextW(hdc, g_combos[i].character, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        rc.left = cardX + 180; rc.right = cardX + cardW - 170;
        rc.top = cardY + 28; rc.bottom = cardY + 60;
        DrawTextW(hdc, g_combos[i].name, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, g_fontCardSmall);
        SetTextColor(hdc, RGB(190, 180, 210));
        rc.left = cardX + 24; rc.top = cardY + 48; rc.right = cardX + 150; rc.bottom = cardY + 68;
        if (strcmp(g_combos[i].mode, "hold") == 0) wcscpy(modeText, L"按住循环");
        else wcscpy(modeText, L"按一次跑完");
        DrawTextW(hdc, modeText, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        /* 触发键小药丸 */
        DisplayTrigger(g_combos[i].trigger, trig, 64);
        SelectObject(hdc, g_fontPill);
        GetTextExtentPoint32W(hdc, trig, (int)wcslen(trig), &sz);
        pillW = sz.cx + 28;
        pillX = cardX + cardW - 18 - pillW;
        pillY = cardY + 26;
        if (strcmp(g_combos[i].trigger, "none") == 0) {
            brush = CreateSolidBrush(RGB(110, 100, 120));
            pen = (HPEN)GetStockObject(NULL_PEN);
            oldBrush = SelectObject(hdc, brush);
            oldPen = SelectObject(hdc, pen);
            RoundRect(hdc, pillX, pillY, pillX + pillW, pillY + 32, 16, 16);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(brush);
        } else {
            HRGN hPillClip = CreateRoundRectRgn(pillX, pillY, pillX + pillW, pillY + 32, 16, 16);
            SelectClipRgn(hdc, hPillClip);
            DeleteObject(hPillClip);
            FillHorizontalGradient(hdc, pillX, pillY, pillW, 32, RGB(139, 92, 246), RGB(192, 132, 252));
            SelectClipRgn(hdc, NULL);
            pen = CreatePen(PS_SOLID, 1, RGB(168, 85, 247));
            oldPen = SelectObject(hdc, pen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, pillX, pillY, pillX + pillW, pillY + 32, 16, 16);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
        SetTextColor(hdc, RGB(255, 255, 255));
        rc.left = pillX; rc.top = pillY; rc.right = pillX + pillW; rc.bottom = pillY + 32;
        DrawTextW(hdc, trig, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
    }
    if (g_count == 0) {
        SetTextColor(hdc, RGB(190, 180, 210));
        oldFont = SelectObject(hdc, g_fontCard);
        rc.left = 24; rc.top = HEADER_H + 30; rc.right = clientW - 24; rc.bottom = HEADER_H + 70;
        DrawTextW(hdc, L"这里还没有连招，请确认 genshin\\combos 文件夹在启动器旁边。", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
    }
    SelectClipRgn(hdc, NULL);
    /* 底栏：截图同款深底 */
    brush = CreateSolidBrush(RGB(16, 9, 30));
    rc.left = 0; rc.top = clientH - FOOTER_H; rc.right = clientW; rc.bottom = clientH;
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    pen = CreatePen(PS_SOLID, 1, RGB(70, 55, 110));
    oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, 0, clientH - FOOTER_H, NULL);
    LineTo(hdc, clientW, clientH - FOOTER_H);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    /* 状态圆点 + 文案 */
    brush = CreateSolidBrush(g_engineRunning ? RGB(74, 222, 128) : RGB(168, 85, 247));
    pen = (HPEN)GetStockObject(NULL_PEN);
    oldBrush = SelectObject(hdc, brush);
    oldPen = SelectObject(hdc, pen);
    Ellipse(hdc, 24, clientH - FOOTER_H + 16, 40, clientH - FOOTER_H + 32);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 210, 235));
    oldFont = SelectObject(hdc, g_fontCardSmall);
    rc.left = 48; rc.top = clientH - FOOTER_H + 10; rc.right = clientW - 24; rc.bottom = clientH - FOOTER_H + 34;
    if (g_engineRunning)
        DrawTextW(hdc, L"连招运行中 · 切到原神，按快捷键就能打", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    else
        DrawTextW(hdc, L"还没开始 · 点右下“开始连招”", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, RGB(150, 140, 170));
    rc.top = clientH - 22; rc.bottom = clientH - 4;
    DrawTextW(hdc, L"关闭窗口时，连招也会一起停止 · 急停按 F12", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;
        g_fontTitle = CreateFontW(-44, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
        g_fontSub = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
        g_fontCard = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
        g_fontCardSmall = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
        g_fontPill = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
        g_fontBtn = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"微软雅黑");
        g_hBtnStart = CreateWindowExW(0, L"BUTTON", L"开始连招", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 128, 48, hwnd, (HMENU)ID_BTN_START, g_hInst, NULL);
        g_hBtnStop = CreateWindowExW(0, L"BUTTON", L"停止", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 128, 48, hwnd, (HMENU)ID_BTN_STOP, g_hInst, NULL);
        g_hBtnEdit = CreateWindowExW(0, L"BUTTON", L"改快捷键", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 128, 48, hwnd, (HMENU)ID_BTN_EDIT, g_hInst, NULL);
        g_hBtnFolder = CreateWindowExW(0, L"BUTTON", L"打开文件夹", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 128, 48, hwnd, (HMENU)ID_BTN_FOLDER, g_hInst, NULL);
        UpdateButtons();
        SetTimer(hwnd, ID_TIMER_ENGINE, 1000, NULL);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        UpdateScroll(h);
        LayoutButtons(w, h);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        PaintMain(hdc, rc.right, rc.bottom);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lParam;
        HBRUSH brush;
        HPEN pen;
        HGDIOBJ oldBrush, oldPen;
        COLORREF bg, fg;
        int disabled = (di->itemState & ODS_DISABLED) != 0;
        int primary = (di->CtlID == ID_BTN_START);
        if (disabled) { bg = RGB(60, 55, 80); fg = RGB(150, 145, 170); }
        else if (primary) { bg = RGB(124, 58, 237); fg = RGB(255, 255, 255); }
        else { bg = RGB(45, 35, 75); fg = RGB(255, 255, 255); }
        if (!disabled && primary) {
            HRGN hBtnClip = CreateRoundRectRgn(di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, 16, 16);
            SelectClipRgn(di->hDC, hBtnClip);
            DeleteObject(hBtnClip);
            FillHorizontalGradient(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right - di->rcItem.left, di->rcItem.bottom - di->rcItem.top, RGB(139, 92, 246), RGB(192, 132, 252));
            SelectClipRgn(di->hDC, NULL);
            pen = CreatePen(PS_SOLID, 1, RGB(192, 132, 252));
            oldPen = SelectObject(di->hDC, pen);
            SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
            RoundRect(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, 16, 16);
            SelectObject(di->hDC, oldPen);
            DeleteObject(pen);
        } else {
            brush = CreateSolidBrush(bg);
            pen = CreatePen(PS_SOLID, 1, disabled ? bg : RGB(122, 106, 168));
            oldBrush = SelectObject(di->hDC, brush);
            oldPen = SelectObject(di->hDC, pen);
            RoundRect(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, 16, 16);
            SelectObject(di->hDC, oldBrush);
            SelectObject(di->hDC, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);
        SelectObject(di->hDC, g_fontBtn);
        {
            wchar_t text[64];
            GetWindowTextW(di->hwndItem, text, 64);
            DrawTextW(di->hDC, text, -1, &di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (y >= HEADER_H && y < rc.bottom - FOOTER_H && x >= 20 && x < rc.right - 20) {
            int rel = y - HEADER_H - 12 + g_scroll;
            int idx = rel / (CARD_H + CARD_GAP);
            int within = rel % (CARD_H + CARD_GAP);
            if (idx >= 0 && idx < g_count && within < CARD_H) {
                g_selected = idx;
                UpdateButtons();
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (y >= HEADER_H && y < rc.bottom - FOOTER_H && x >= 20 && x < rc.right - 20) {
            int rel = y - HEADER_H - 12 + g_scroll;
            int idx = rel / (CARD_H + CARD_GAP);
            int within = rel % (CARD_H + CARD_GAP);
            if (idx >= 0 && idx < g_count && within < CARD_H) {
                g_selected = idx;
                UpdateButtons();
                InvalidateRect(hwnd, NULL, TRUE);
                OpenHotkeyDialog();
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        g_scroll -= (delta / WHEEL_DELTA) * 50;
        if (g_scroll < 0) g_scroll = 0;
        {
            int max = MaxScroll(rc.bottom);
            if (g_scroll > max) g_scroll = max;
        }
        SetScrollPos(hwnd, SB_VERT, g_scroll, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_VSCROLL: {
        RECT rc;
        int max, vis, code = LOWORD(wParam);
        GetClientRect(hwnd, &rc);
        max = MaxScroll(rc.bottom);
        vis = rc.bottom - HEADER_H - FOOTER_H;
        if (code == SB_LINEUP) g_scroll -= 50;
        else if (code == SB_LINEDOWN) g_scroll += 50;
        else if (code == SB_PAGEUP) g_scroll -= vis;
        else if (code == SB_PAGEDOWN) g_scroll += vis;
        else if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) g_scroll = HIWORD(wParam);
        else if (code == SB_TOP) g_scroll = 0;
        else if (code == SB_BOTTOM) g_scroll = max;
        if (g_scroll < 0) g_scroll = 0;
        if (g_scroll > max) g_scroll = max;
        SetScrollPos(hwnd, SB_VERT, g_scroll, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == ID_TIMER_ENGINE) CheckEngineExited();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_START) StartEngine();
        else if (id == ID_BTN_STOP) StopEngine();
        else if (id == ID_BTN_EDIT) OpenHotkeyDialog();
        else if (id == ID_BTN_FOLDER) ShellExecuteW(NULL, L"explore", g_comboDir, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }
    case WM_CLOSE:
        StopEngine();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_ENGINE);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontSub) DeleteObject(g_fontSub);
        if (g_fontCard) DeleteObject(g_fontCard);
        if (g_fontCardSmall) DeleteObject(g_fontCardSmall);
        if (g_fontPill) DeleteObject(g_fontPill);
        if (g_fontBtn) DeleteObject(g_fontBtn);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR pCmd, int nCmdShow) {
    WNDCLASSEXW wcMain, wcDlg;
    HWND hwnd;
    HANDLE mutex;
    MSG msg;
    (void)hPrev; (void)pCmd;
    g_hInst = hInstance;
    mutex = CreateMutexW(NULL, FALSE, L"GenshinComboLauncherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"GenshinComboLauncher", NULL);
        if (prev) { ShowWindow(prev, SW_RESTORE); SetForegroundWindow(prev); }
        return 0;
    }
    memset(&wcMain, 0, sizeof(wcMain));
    wcMain.cbSize = sizeof(wcMain);
    wcMain.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcMain.lpfnWndProc = MainProc;
    wcMain.hInstance = hInstance;
    wcMain.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wcMain.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wcMain.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcMain.hbrBackground = CreateSolidBrush(RGB(24, 13, 48));
    wcMain.lpszClassName = L"GenshinComboLauncher";
    RegisterClassExW(&wcMain);
    memset(&wcDlg, 0, sizeof(wcDlg));
    wcDlg.cbSize = sizeof(wcDlg);
    wcDlg.style = CS_HREDRAW | CS_VREDRAW;
    wcDlg.lpfnWndProc = DialogProc;
    wcDlg.hInstance = hInstance;
    wcDlg.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcDlg.hbrBackground = CreateSolidBrush(RGB(24, 14, 44));
    wcDlg.lpszClassName = L"GenshinHotkeyDialog";
    RegisterClassExW(&wcDlg);
    BuildPaths();
    LoadCombos();
    hwnd = CreateWindowExW(0, L"GenshinComboLauncher", L"原神连招小助手",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 980, 740,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    g_hMain = hwnd;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hDlg && IsDialogMessageW(g_hDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
