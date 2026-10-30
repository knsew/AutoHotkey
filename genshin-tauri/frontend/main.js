// 原神连招小助手 - Tauri 版前端（纯 JS，无构建步骤）
// 后端命令见 src-tauri/src/main.rs。invoke 走 window.__TAURI__.core，
// tauri.conf.json 里 withGlobalTauri: true，所以不用 npm 装 @tauri-apps/api。
const invoke = (...args) => window.__TAURI__.core.invoke(...args);

let combos = [];
let tab = 'all'; // all | fav | bound
let charFilter = '';
let search = '';
let selectedFile = null;
let running = false;
let editingFile = null;

const $ = (id) => document.getElementById(id);
const listEl = $('list');

function displayTrigger(trig) {
  if (trig === 'none') return '未绑定';
  let out = '';
  for (const ch of trig) {
    if (ch === '^') out += 'Ctrl+';
    else if (ch === '+') out += 'Shift+';
    else if (ch === '!') out += 'Alt+';
    else if (ch === '#') out += 'Win+';
    else out += ch;
  }
  return out;
}

// AHK 写法校验：跟 C 版同一口径——必须带修饰键，单按数字/字母不给绑
// （数字在原神里本身就是切人键），且不能跟别的套重复。
function parseFriendly(input) {
  const s = input.replace(/\s+/g, '');
  if (!s) return null;
  if (/^[\^\+!#]/.test(s)) {
    if (!/^[A-Za-z0-9]/.test(s.slice(-1)) || s.length < 2) return null;
    for (const ch of s) if (ch.charCodeAt(0) > 127) return null;
    return s.toLowerCase().replace(/[A-Z]/g, (c) => c.toLowerCase());
  }
  const parts = s.split('+');
  let ctrl = false, shift = false, alt = false, win = false, last = null;
  for (const raw of parts) {
    const tok = raw.toLowerCase();
    if (tok === 'ctrl') ctrl = true;
    else if (tok === 'shift') shift = true;
    else if (tok === 'alt') alt = true;
    else if (tok === 'win') win = true;
    else if (tok.length === 1) last = tok;
    else return null;
  }
  if (!last || !(ctrl || shift || alt || win)) return null;
  return (ctrl ? '^' : '') + (shift ? '+' : '') + (alt ? '!' : '') + (win ? '#' : '') + last;
}

function filtered() {
  const q = search.trim().toLowerCase();
  return combos.filter((c) => {
    if (tab === 'fav' && !c.fav) return false;
    if (tab === 'bound' && c.trigger === 'none') return false;
    if (charFilter && c.character !== charFilter) return false;
    if (q && !(c.character.toLowerCase().includes(q) || c.name.toLowerCase().includes(q))) return false;
    return true;
  });
}

function render() {
  const view = filtered();
  const favCount = combos.filter((c) => c.fav).length;
  $('countText').textContent = `显示 ${view.length} / ${combos.length} 套 · 收藏 ${favCount} 套 · ↑↓选卡`;
  document.querySelectorAll('.tab').forEach((b) => {
    b.classList.toggle('active', b.dataset.tab === tab);
  });
  listEl.innerHTML = '';
  if (view.length === 0) {
    const div = document.createElement('div');
    div.className = 'empty';
    div.textContent = combos.length === 0
      ? '这里还没有连招，请确认 genshin\\combos 文件夹在启动器旁边。'
      : '没有符合条件的连招，点筛选条右边的“清除”试试。';
    listEl.appendChild(div);
    return;
  }
  view.forEach((c) => {
    const card = document.createElement('button');
    card.className = 'card' + (c.file === selectedFile ? ' selected' : '');
    card.dataset.file = c.file;
    card.innerHTML = `
      <span class="card-char"><b>${c.character}</b><span>${c.mode === 'hold' ? '按住循环' : '按一次跑完'}</span></span>
      <span class="card-name">${c.name}</span>
      <span class="star ${c.fav ? 'fav' : ''}" role="button" aria-label="收藏">${c.fav ? '★' : '☆'}</span>
      <span class="pill ${c.trigger === 'none' ? 'none' : ''}">${displayTrigger(c.trigger)}</span>`;
    card.addEventListener('click', () => { select(c.file, true); });
    card.addEventListener('dblclick', () => { select(c.file, false); openDialog(c.file); });
    card.querySelector('.star').addEventListener('click', async (e) => {
      e.stopPropagation();
      await invoke('toggle_favorite', { file: c.file });
      await refresh(false);
    });
    listEl.appendChild(card);
  });
  $('editBtn').disabled = !selectedFile;
}

function select(file, scrollIntoView) {
  selectedFile = file;
  render();
  if (scrollIntoView) {
    const el = listEl.querySelector(`[data-file="${CSS.escape(file)}"]`);
    if (el) el.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }
}

async function refresh(keepSelection) {
  const prev = selectedFile;
  combos = await invoke('list_combos');
  // 角色下拉：按首次出现顺序，不重复
  const sel = $('charSelect');
  const cur = sel.value;
  const seen = new Set();
  sel.innerHTML = '<option value="">全部角色</option>';
  combos.forEach((c) => {
    if (!seen.has(c.character)) {
      seen.add(c.character);
      const opt = document.createElement('option');
      opt.value = c.character;
      opt.textContent = c.character;
      sel.appendChild(opt);
    }
  });
  if (charFilter && seen.has(charFilter)) sel.value = charFilter;
  else if (cur && seen.has(cur)) { sel.value = cur; charFilter = cur; }
  else { sel.value = ''; charFilter = ''; }
  if (keepSelection && prev && combos.some((c) => c.file === prev)) selectedFile = prev;
  else if (!filtered().some((c) => c.file === selectedFile)) selectedFile = null;
  render();
}

async function updateRunning() {
  running = await invoke('engine_running');
  $('statusDot').classList.toggle('running', running);
  $('statusText').textContent = running ? '连招运行中 · 切到原神，按快捷键就能打' : '还没开始 · 点右下“开始连招”';
  $('startBtn').disabled = running;
  $('stopBtn').disabled = !running;
}

function openDialog(file) {
  const c = combos.find((x) => x.file === file);
  if (!c) return;
  editingFile = file;
  $('hotkeyInput').value = c.trigger === 'none' ? '' : displayTrigger(c.trigger);
  $('modalError').hidden = true;
  $('modalBackdrop').hidden = false;
  $('hotkeyInput').focus();
  $('hotkeyInput').select();
}
function closeDialog() {
  $('modalBackdrop').hidden = true;
  editingFile = null;
}

async function applyTrigger(newTrig) {
  // 正在运行就先停、改完再起，跟 C 版 ApplyHotkey 同一口径
  const wasRunning = running;
  if (wasRunning) await invoke('stop_engine');
  await invoke('update_trigger', { file: editingFile, trigger: newTrig });
  await refresh(true);
  if (wasRunning) await invoke('start_engine');
  await updateRunning();
  closeDialog();
}

// 筛选条事件
document.querySelectorAll('.tab').forEach((b) => {
  b.addEventListener('click', () => { tab = b.dataset.tab; render(); });
});
$('charSelect').addEventListener('change', (e) => { charFilter = e.target.value; render(); });
$('searchInput').addEventListener('input', (e) => { search = e.target.value; render(); });
$('clearBtn').addEventListener('click', () => {
  tab = 'all'; charFilter = ''; search = '';
  $('charSelect').value = ''; $('searchInput').value = '';
  render();
});

// 底栏按钮
$('startBtn').addEventListener('click', async () => {
  try { await invoke('start_engine'); } catch (e) { alert(e); }
  await updateRunning();
});
$('stopBtn').addEventListener('click', async () => { await invoke('stop_engine'); await updateRunning(); });
$('editBtn').addEventListener('click', () => { if (selectedFile) openDialog(selectedFile); });
$('folderBtn').addEventListener('click', async () => { await invoke('open_combo_folder'); });

// 弹窗按钮
$('saveBtn').addEventListener('click', async () => {
  const ahk = parseFriendly($('hotkeyInput').value);
  const err = $('modalError');
  if (!ahk) {
    err.textContent = '格式不对哦，只支持 Ctrl / Shift / Alt / Win + 一个字母或数字，例如 Ctrl+1、Ctrl+Shift+1。';
    err.hidden = false; return;
  }
  const clash = combos.find((c) => c.file !== editingFile && c.trigger === ahk);
  if (clash) {
    err.textContent = `这个快捷键已经被“${clash.character} · ${clash.name}”占了，换一个吧。`;
    err.hidden = false; return;
  }
  try { await applyTrigger(ahk); } catch (e) { err.textContent = String(e); err.hidden = false; }
});
$('cancelBtn').addEventListener('click', closeDialog);
$('disableBtn').addEventListener('click', async () => {
  try { await applyTrigger('none'); } catch (e) { alert(e); }
});
$('modalBackdrop').addEventListener('click', (e) => {
  if (e.target === $('modalBackdrop')) closeDialog();
});

// 键盘：↑↓选卡并平滑滚到可见，/ 聚焦搜索，Enter 改快捷键
document.addEventListener('keydown', (e) => {
  if (!$('modalBackdrop').hidden) {
    if (e.key === 'Escape') closeDialog();
    if (e.key === 'Enter') $('saveBtn').click();
    return;
  }
  if (e.key === '/') { e.preventDefault(); $('searchInput').focus(); return; }
  if (e.key === 'Enter' && selectedFile) { openDialog(selectedFile); return; }
  const view = filtered();
  if (view.length === 0) return;
  let idx = view.findIndex((c) => c.file === selectedFile);
  if (e.key === 'ArrowDown') { idx = idx < 0 ? 0 : Math.min(idx + 1, view.length - 1); select(view[idx].file, true); e.preventDefault(); }
  else if (e.key === 'ArrowUp') { idx = idx <= 0 ? 0 : idx - 1; select(view[idx].file, true); e.preventDefault(); }
  else if (e.key === 'Home') { select(view[0].file, true); e.preventDefault(); }
  else if (e.key === 'End') { select(view[view.length - 1].file, true); e.preventDefault(); }
});

refresh(false).then(updateRunning);
setInterval(updateRunning, 1000);
