// 原神连招小助手 - Tauri 版启动器（Rust 后端）
//
// 只做 C 版启动器做过的事，换一层皮：
//   读 genshin/combos/*.combo（@character/@name/@trigger/@mode）
//   改某套的 @trigger（其余行原样保留，UTF-8 BOM 保留）
//   收藏写 genshin/favorites.txt（跟连招放一起，便携）
//   启动/停止 bin/AutoHotkey64.exe genshin/engine.ahk（按 PID 关，
//   关闭启动器窗口时引擎一起停，不留后台进程）
//
// 路径全部按 exe 所在目录算，不用 %APPDATA%，文件夹拷走就带走。
// 前端是纯 HTML/CSS/JS（frontend/），没有 npm 构建步骤。

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use std::collections::HashSet;
use std::fs;
use std::path::PathBuf;
use std::process::{Child, Command};
use std::sync::Mutex;
use tauri::{Manager, State, WindowEvent};

#[derive(Serialize, Clone)]
struct Combo {
    file: String,
    character: String,
    name: String,
    trigger: String,
    mode: String,
    fav: bool,
}

struct EngineState {
    child: Mutex<Option<Child>>,
}

fn exe_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| PathBuf::from("."))
}

fn combo_dir() -> PathBuf {
    exe_dir().join("genshin").join("combos")
}

fn fav_path() -> PathBuf {
    exe_dir().join("genshin").join("favorites.txt")
}

fn load_fav_names() -> HashSet<String> {
    let mut set = HashSet::new();
    if let Ok(text) = fs::read_to_string(fav_path()) {
        for line in text.lines() {
            let t = line.trim();
            if !t.is_empty() {
                set.insert(t.to_string());
            }
        }
    }
    set
}

fn parse_combo(path: &PathBuf) -> Option<Combo> {
    let bytes = fs::read(path).ok()?;
    let start = if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
        3
    } else {
        0
    };
    let text = String::from_utf8_lossy(&bytes[start..]).to_string();
    let mut character = String::new();
    let mut name = String::new();
    let mut trigger = "none".to_string();
    let mut mode = "once".to_string();
    for raw in text.lines() {
        let line = raw.trim();
        if !line.starts_with('@') {
            continue;
        }
        let mut parts = line[1..].splitn(2, |c| c == ' ' || c == '\t');
        let key = parts.next().unwrap_or("");
        let val = parts.next().unwrap_or("").trim();
        match key {
            "character" => character = val.to_string(),
            "name" => name = val.to_string(),
            "trigger" => trigger = val.to_string(),
            "mode" => mode = val.to_string(),
            _ => {}
        }
    }
    if character.is_empty() || name.is_empty() {
        return None;
    }
    let file = path
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_default();
    Some(Combo {
        file,
        character,
        name,
        trigger,
        mode,
        fav: false,
    })
}

#[tauri::command]
fn list_combos() -> Vec<Combo> {
    let favs = load_fav_names();
    let mut files: Vec<PathBuf> = Vec::new();
    if let Ok(entries) = fs::read_dir(combo_dir()) {
        for e in entries.flatten() {
            let p = e.path();
            if p.extension().map(|x| x == "combo").unwrap_or(false) {
                files.push(p);
            }
        }
    }
    files.sort();
    let mut combos: Vec<Combo> = files.iter().filter_map(parse_combo).collect();
    for c in combos.iter_mut() {
        c.fav = favs.contains(&c.file);
    }
    // 已绑定的排前面，没绑定的排后面，各自保持文件名顺序（stable sort）。
    combos.sort_by(|a, b| {
        let ab = a.trigger != "none";
        let bb = b.trigger != "none";
        bb.cmp(&ab)
    });
    combos
}

#[tauri::command]
fn toggle_favorite(file: String) -> bool {
    let combos = list_combos();
    let mut favs = load_fav_names();
    let mut now_fav = false;
    for c in &combos {
        if c.file == file {
            now_fav = !c.fav;
        }
    }
    if now_fav {
        favs.insert(file);
    } else {
        favs.remove(&file);
    }
    let mut names: Vec<String> = favs.into_iter().collect();
    names.sort();
    let out = names.join("\n");
    let _ = fs::write(fav_path(), if out.is_empty() { out } else { format!("{}\n", out) });
    now_fav
}

#[tauri::command]
fn update_trigger(file: String, trigger: String) -> Result<(), String> {
    let path = combo_dir().join(&file);
    let bytes = fs::read(&path).map_err(|e| format!("读文件失败：{}", e))?;
    let has_bom = bytes.starts_with(&[0xEF, 0xBB, 0xBF]);
    let start = if has_bom { 3 } else { 0 };
    let text = String::from_utf8_lossy(&bytes[start..]).to_string();
    let mut out = String::new();
    let mut found = false;
    for raw in text.lines() {
        let trimmed = raw.trim();
        if trimmed.starts_with("@trigger") {
            let rest = &trimmed["@trigger".len()..];
            if rest.is_empty() || rest.starts_with(' ') || rest.starts_with('\t') {
                out.push_str(&format!("@trigger {}\n", trigger));
                found = true;
                continue;
            }
        }
        out.push_str(raw.trim_end());
        out.push('\n');
    }
    if !found {
        out.push_str(&format!("@trigger {}\n", trigger));
    }
    let mut final_bytes = Vec::new();
    if has_bom {
        final_bytes.extend_from_slice(&[0xEF, 0xBB, 0xBF]);
    }
    final_bytes.extend_from_slice(out.as_bytes());
    fs::write(&path, final_bytes).map_err(|e| format!("写文件失败：{}", e))?;
    Ok(())
}

fn kill_child(state: &EngineState) {
    if let Ok(mut guard) = state.child.lock() {
        if let Some(mut child) = guard.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
    }
}

#[tauri::command]
fn start_engine(state: State<'_, EngineState>) -> Result<u32, String> {
    kill_child(&state);
    let interp = exe_dir().join("bin").join("AutoHotkey64.exe");
    let engine = exe_dir().join("genshin").join("engine.ahk");
    if !interp.exists() {
        return Err("找不到解释器 bin\\AutoHotkey64.exe，请确认它在启动器旁边。".to_string());
    }
    if !engine.exists() {
        return Err("找不到连招引擎 genshin\\engine.ahk，请确认文件夹完整。".to_string());
    }
    let child = Command::new(interp)
        .arg(engine)
        .spawn()
        .map_err(|e| format!("启动连招失败：{}，请右键启动器选择“以管理员身份运行”再试一次。", e))?;
    let pid = child.id();
    if let Ok(mut guard) = state.child.lock() {
        *guard = Some(child);
    }
    Ok(pid)
}

#[tauri::command]
fn stop_engine(state: State<'_, EngineState>) -> Result<(), String> {
    kill_child(&state);
    Ok(())
}

#[tauri::command]
fn engine_running(state: State<'_, EngineState>) -> bool {
    if let Ok(mut guard) = state.child.lock() {
        if let Some(child) = guard.as_mut() {
            match child.try_wait() {
                Ok(None) => return true,
                Ok(Some(_)) => {
                    *guard = None;
                    return false;
                }
                Err(_) => return false,
            }
        }
    }
    false
}

#[tauri::command]
fn open_combo_folder() -> Result<(), String> {
    let dir = combo_dir();
    #[cfg(target_os = "windows")]
    {
        Command::new("explorer")
            .arg(dir)
            .spawn()
            .map_err(|e| format!("打开文件夹失败：{}", e))?;
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = dir;
    }
    Ok(())
}

fn main() {
    tauri::Builder::default()
        .manage(EngineState {
            child: Mutex::new(None),
        })
        .invoke_handler(tauri::generate_handler![
            list_combos,
            toggle_favorite,
            update_trigger,
            start_engine,
            stop_engine,
            engine_running,
            open_combo_folder
        ])
        .on_window_event(|window, event| {
            // 关闭启动器窗口时，引擎一起停，不留后台进程（跟 C 版同一口径）。
            if let WindowEvent::CloseRequested { .. } = event {
                if let Some(state) = window.try_state::<EngineState>() {
                    kill_child(&state);
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("启动 Tauri 应用失败");
}
