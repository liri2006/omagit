// Screen composers: top bar (with the page tabs), pages, diff pane, action bar, footer, menus, dialogs.
const K = require('./kit.js');
const { rect, border, fillBox, hairline, vline, text, icon, circle, button, segmented, field, checkbox, sectionLabel, dimText, refChip,
  statusColor, statusPill, scrollbar, badge, tw, SIZE, measureButton } = K;
const T = () => K.theme();

// ------------------------------------------------------------ breakpoints
// Width classes in px at base font 12 (everything scales with base/12).
function levelFor(W) { return W >= 1400 ? 'xl' : W >= 1000 ? 'l' : W >= 700 ? 'm' : W >= 480 ? 's' : 'xs'; }
const wide = lv => lv === 'xl' || lv === 'l';
const stacked = lv => lv === 's' || lv === 'xs';

// ------------------------------------------------------------ sample data
const FILES = [
  { name: 'GitRepo.cpp', path: 'src/core', ext: '.cpp', size: '7.1 KiB', st: 'M', add: 1, del: 1, checked: true },
  { name: 'RemoteSync.cpp', path: 'src/core', ext: '.cpp', size: '4.8 KiB', st: 'M', add: 1, del: 0, checked: true },
  { name: 'Toolbar.cpp', path: 'src/ui', ext: '.cpp', size: '6.2 KiB', st: 'M', add: 4, del: 2, checked: true, selected: true },
  { name: 'README.md', path: 'docs', ext: '.md', size: '2.0 KiB', st: 'D', add: 0, del: 60, checked: true },
  { name: 'ui_tests.cpp', path: 'tests', ext: '.cpp', size: '9.4 KiB', st: 'R', add: 1, del: 0, checked: true },
  { name: 'notes.txt', path: '', ext: '.txt', size: '6 B', st: '?', add: 0, del: 0, checked: false },
  { name: 'NewWidget.cpp', path: 'src/ui', ext: '.cpp', size: '9 B', st: '?', add: 0, del: 0, checked: false },
];
const COMMITS = [
  { sha: 'd444446', msg: 'Make the toolbar tiling aware', author: 'Andras', date: '2026-09-18 10:55', refs: [['main', 'head'], ['origin/main', 'remote']], lane: 0 },
  { sha: '6bd79ab', msg: 'Refine clone dialog layout and GitHub repository states', author: 'Andras', date: '2026-09-17 15:52', refs: [], lane: 0 },
  { sha: '14d6d14', msg: 'Add repository cloning with GitHub browser integration', author: 'Andras', date: '2026-09-17 12:10', refs: [['v0.4', 'tag']], lane: 0 },
  { sha: '2258b52', msg: 'Add Codex-assisted briefing and review workflows', author: 'Andras', date: '2026-09-16 18:31', refs: [['feature/askpass', 'local']], lane: 1, merge: true },
  { sha: '264ad70', msg: 'Add themed askpass login support for remote operations', author: 'Andras', date: '2026-09-16 09:02', refs: [], lane: 1 },
  { sha: '571c560', msg: 'Refine keybindings list scrolling with row peeks', author: 'Andras', date: '2026-09-15 20:44', refs: [], lane: 0 },
  { sha: '9d101a1', msg: 'Add lazygit-style keybindings and the Ctrl+K panel', author: 'Andras', date: '2026-09-15 11:20', refs: [], lane: 0 },
  { sha: '3fa02c9', msg: 'Split MainWindow into CommitPage, DiffPane and Footer', author: 'Andras', date: '2026-09-10 17:05', refs: [], lane: 0 },
  { sha: 'b71e0d2', msg: 'Grow the commit message box with its content', author: 'Andras', date: '2026-09-10 13:40', refs: [], lane: 0 },
  { sha: 'aa90c11', msg: 'Add merge view with merge-tree preview', author: 'Andras', date: '2026-09-07 19:20', refs: [], lane: 0 },
  { sha: '0c4e77d', msg: 'Add fetch, pull and push with ahead/behind badges', author: 'Andras', date: '2026-09-06 22:15', refs: [], lane: 0 },
  { sha: '77b3d90', msg: 'Initial import of the project skeleton', author: 'Andras', date: '2026-09-06 09:00', refs: [], lane: 0 },
];
const CODE = [
  '#include "Toolbar.h"', '#include "UiHelpers.h"', '', '#include <QHBoxLayout>', '#include <QResizeEvent>', '#include <QToolButton>', '',
  'using namespace ui;', '', 'Toolbar::Toolbar(QWidget *parent)', '    : QWidget(parent)', '{',
  '    auto *row = new QHBoxLayout(this);', '    row->setContentsMargins(0, 0, 0, 0);', '    row->setSpacing(kSpacing);',
  '    m_more = new BadgeButton(this);', '    m_more->setText(icon(kDots));', '    m_more->hide();', '}', '',
  'void Toolbar::addButton(QToolButton *button, const QString &full,', '                        const QString &icon, const QString &menu)', '{',
  '    m_items.append({button, nullptr, full, icon, menu});', '    layout()->addWidget(button);', '    measure();', '}', '',
  'void Toolbar::resizeEvent(QResizeEvent *event)', '{', '    QWidget::resizeEvent(event);', '    relayout();', '}', '',
  '// Labels give way to icons, icons to the more menu.', 'void Toolbar::relayout()', '{', '    const int width = this->width();',
  '    bool full = fits(width, true);', '    for (Item &item : m_items)', '        item.button->setText(full ? item.full : item.icon);',
  '    updateMoreMark();', '}', '', 'QSize Toolbar::sizeHint() const', '{', '    return {m_items.size() * 80, m_height};', '}', '',
  'QSize Toolbar::minimumSizeHint() const', '{', '    return {m_leading ? m_leading->width() : 0, m_height};', '}', '',
  'void Toolbar::applyTheme()', '{', '    measure();', '    relayout();', '}',
];
// A diff: rows of {l:{n,text,kind}, r:{n,text,kind}} for split; unified derived from it.
function buildDiff() {
  const rows = []; let ln = 1, rn = 1;
  CODE.forEach((line, i) => {
    if (i === 38) {
      rows.push({ l: { n: ln++, t: line, k: 'del', hl: [[4, 8], [21, 26]] }, r: { n: rn++, t: '    const Level level = levelFor(width, height());', k: 'add', hl: [[4, 9], [21, 32], [40, 48]] } });
      return;
    }
    if (i === 40) {
      rows.push({ l: { n: ln++, t: line, k: 'del', hl: [[30, 34]] }, r: { n: rn++, t: '        item.button->setText(textFor(item, level));', k: 'add', hl: [[30, 46]] } });
      rows.push({ l: { k: 'filler' }, r: { n: rn++, t: '    m_more->setVisible(level == Level::Overflow);', k: 'add' } });
      return;
    }
    rows.push({ l: { n: ln++, t: line, k: 'ctx' }, r: { n: rn++, t: line, k: 'ctx' } });
  });
  return rows;
}
const DIFF = buildDiff();

// crude C++ colouring: returns [{s, fill}]
function tokens(line) {
  const t = T(); const out = [];
  if (/^\s*\/\//.test(line)) return [{ s: line, fill: t.dim }];
  if (/^#include/.test(line)) { const m = line.match(/^(#include\s*)(.*)$/); return [{ s: m[1], fill: t.magenta }, { s: m[2], fill: t.green }]; }
  const re = /(\b(?:void|const|auto|bool|int|using|namespace|return|new|for|this|true|false|QSize|QWidget|QString|QToolButton|QResizeEvent|QHBoxLayout|Item|Level)\b)|("[^"]*")|(\b\d+\b)|(\/\/.*$)/g;
  let last = 0, m;
  while ((m = re.exec(line))) {
    if (m.index > last) out.push({ s: line.slice(last, m.index), fill: t.fg });
    out.push({ s: m[0], fill: m[1] ? t.blue : m[2] ? t.green : m[3] ? t.yellow : t.dim });
    last = m.index + m[0].length;
  }
  if (last < line.length) out.push({ s: line.slice(last), fill: t.fg });
  return out;
}

// ------------------------------------------------------------ top bar
// Repo + branch context on the left, sync + layout on the right.
function topBar(c, W, lv, o = {}) {
  const t = T(), h = 40, y = 6; let x = 12;
  c.group('TopBar', () => {
    // repo chip: label + chevron when there is room, a bare folder icon when stacked (never hidden)
    const w = button(c, { x, y, variant: 'ghost', icon: 'folderOpen', label: stacked(lv) ? '' : 'omagit', chevron: !stacked(lv), id: 'RepoChip' });
    x += w + 4;
    // branch chip (accent, bold)
    const branch = o.merging ? 'main · merging feature/askpass' : 'main';
    const bw = button(c, { x, y, variant: 'ghost', icon: 'branch', label: branch, chevron: true, weight: 700, iconFill: t.accent, id: 'BranchChip' });
    // paint label accent: overlay
    c.add(`<g id="BranchChip/label-accent">`); text(c, x + 10 + 14 + 6, y + 14, branch, { weight: 700, fill: t.accent }); c.add('</g>');
    x += bw;
    if (o.merging) { rect(c, x + 2, y + 6, 16, 16, { fill: t.red, fo: 0.18 }); text(c, x + 10, y + 14, '!', { size: 10, weight: 700, fill: t.red, anchor: 'middle' }); x += 20; }
    // right side
    let rx = W - 12;
    if (wide(lv) || lv === 'm') {
      // layout toggles
      rx -= 28; button(c, { x: rx, y, w: 28, variant: 'ghost', icon: 'dockRight', state: o.diffHidden ? 'normal' : 'selected', id: 'Toggle/diff pane' });
      rx -= 4 + 28; button(c, { x: rx, y, w: 28, variant: 'ghost', icon: 'mini', state: o.mini ? 'selected' : 'normal', id: 'Toggle/mini' });
      rx -= 10; vline(c, rx, y + 6, 16, { fo: 0.2 }); rx -= 10;
      if (lv === 'm') { rx -= 28; button(c, { x: rx, y, w: 28, icon: 'dots', id: 'More' }); rx -= 6; }
      const labels = lv === 'xl';
      const items = lv === 'm' ? [['push', 'Push', 1], ['pull', 'Pull', 2]] : [['merge', 'Merge'], ['fetch', 'Fetch'], ['push', 'Push', 1], ['pull', 'Pull', 2]];
      items.forEach(([ic, lb, b]) => {
        const w = measureButton({ icon: ic, label: labels ? lb : '' });
        rx -= w; button(c, { x: rx, y, icon: ic, label: labels ? lb : '', badge: b, mark: ic === 'merge' && o.merging, id: 'Sync/' + lb }); rx -= 6;
      });
    } else {
      rx -= 28; button(c, { x: rx, y, w: 28, icon: 'dots', id: 'More' }); rx -= 6;
      // one sync dropdown carrying both counts
      const w = 92; rx -= w;
      c.record({ k: 'SyncDropdown', id: 'SyncDropdown', x: rx, y, w, h: 28, down: 2, up: 1 });
      c.group('SyncDropdown', () => {
        fillBox(c, rx, y, w, 28, 0.04); border(c, rx, y, w, 28, { stroke: t.fg, so: 0.4 });
        icon(c, 'down', rx + 8, y + 7, 14); text(c, rx + 24, y + 14, '2', { weight: 700, fill: t.accent });
        icon(c, 'up', rx + 38, y + 7, 14); text(c, rx + 54, y + 14, '1', { weight: 700, fill: t.accent });
        icon(c, 'chevron', rx + 68, y + 8, 12, { opacity: 0.7 });
      });
    }
    // page tabs: a view toggle centred in the window, like a toolbar mode switch.
    // It keeps clear of the repo/branch group and the sync group: nudged aside first, labels → icons second.
    {
      const lo = x + 16, hi = rx - 16;
      let items = navItems(lv, o.page || 'changes', true), w = measureSegmented(items);
      if (w > hi - lo) { items = navItems(lv, o.page || 'changes', false); w = measureSegmented(items); }
      const sx = Math.max(lo, Math.min(Math.round(W / 2 - w / 2), hi - w));
      segmented(c, { x: sx, y, id: 'NavTabs', items });
    }
    hairline(c, 0, h - 1, W, { fo: 0.12 });
  });
  return h;
}

// ------------------------------------------------------------ page tabs
// The page tabs: Changes | History, plus Diff when the layout is stacked.
function navItems(lv, page, labels) {
  const items = [{ icon: 'commit', label: labels ? 'Changes' : '', count: 7, selected: page === 'changes' }];
  if (stacked(lv)) items.push({ icon: 'diff', label: labels ? 'Diff' : '', selected: page === 'diff' });
  items.push({ icon: 'history', label: labels ? 'History' : '', selected: page === 'history' });
  return items;
}
const measureSegmented = items => items.reduce((a, it) => a + 24 + (it.icon ? 14 + (it.label ? 6 : 0) : 0) + (it.label ? tw(it.label) : 0) + (it.count !== undefined ? 6 + tw(String(it.count), 10) + 8 : 0), 0);


// ------------------------------------------------------------ changes page
function changesPage(c, x, y, w, h, lv, o = {}) {
  const t = T(); let cy = y;
  const shallow = o.shallow;
  c.group('CommitPage', () => {
    // Section header rows are 24 px (label and 24 px icon buttons centred),
    // content follows 6 px below, and sections sit 16 px apart.
    const HR = 24, HGAP = 6, SGAP = 16;
    // MESSAGE
    sectionLabel(c, x, cy + HR / 2, 'Message');
    button(c, { x: x + w - 26, y: cy, w: 24, h: 24, variant: 'ghost', icon: 'cog', id: 'AgentSettings' });
    cy += HR + HGAP;
    const mh = shallow ? 34 : lv === 'xl' ? 96 : lv === 'l' ? 84 : 68;
    c.group('MessageBox', () => {
      fillBox(c, x, cy, w, mh, 0.04); border(c, x, cy, w, mh, { stroke: t.fg, so: 0.4 });
      if (o.message) {
        text(c, x + 8, cy + 14, o.message, { fill: t.fg });
        if (o.messageBody && !shallow) o.messageBody.forEach((l, i) => text(c, x + 8, cy + 14 + 18 * (i + 1), l, { fill: t.fg }));
      } else text(c, x + 8, cy + 14, 'Commit message', { fill: t.dim });
      button(c, { x: x + w - 26, y: cy + 2, w: 24, h: 24, variant: 'ghost', icon: 'sparkle', id: 'Generate' });
    });
    cy += mh + SGAP;
    // CHANGES header: the title carries the checked / total count (it used to be
    // a separate "n / m selected" label, which crowded the row on narrow panes)
    const files = o.rows || FILES, checked = files.filter(f => f.checked).length;
    sectionLabel(c, x, cy + HR / 2, 'Changes · ' + checked + '/' + files.length, { id: 'Section/Changes' });
    // how the files are listed: table (wide default), compact (narrow default), tree
    // …then a divider and the unversioned-files filter, which acts on the same list
    // …and Refresh at the far right behind its own divider (it reloads this list)
    let ex = x + w - 26;
    button(c, { x: ex, y: cy, w: 24, h: 24, variant: 'ghost', icon: 'refresh', id: 'Refresh' }); vline(c, ex - 8, cy + 3, 18, { fo: 0.25 }); ex -= 38;
    const view = filesView(lv, o), vx = ex - 12 - 3 * 26;
    FILE_VIEWS.forEach(([id, ic], i) => button(c, { x: vx + i * 26, y: cy, w: 24, h: 24, variant: 'ghost', icon: ic, state: view === id ? 'selected' : 'normal', id: 'FilesView/' + id }));
    vline(c, vx + 3 * 26 + 4, cy + 3, 18, { fo: 0.25 });
    button(c, { x: ex, y: cy, w: 24, h: 24, variant: 'ghost', icon: 'eye', state: o.unversioned === false ? 'normal' : 'selected', id: 'ShowUnversioned' });
    cy += HR + HGAP;
    // table
    const tableH = h - (cy - y) - 48;
    changesTable(c, x, cy, w, tableH, lv, o);
    cy += tableH;
    // action bar
    actionBar(c, x, cy + 8, w, lv, o);
  });
}

const FILE_VIEWS = [['compact', 'list'], ['tree', 'tree'], ['table', 'table']];
const filesView = (lv, o) => o.filesView || (stacked(lv) ? 'compact' : 'table');
// lazygit-style tree: directories first (each level nested), then the files of
// that level; o.collapsed lists directory paths shown folded.
function fileTree(rows, collapsed = []) {
  const root = { dirs: {}, files: [], path: '' };
  rows.forEach(f => { let n = root; (f.path ? f.path.split('/') : []).forEach(p => { n = n.dirs[p] || (n.dirs[p] = { name: p, dirs: {}, files: [], path: n.path ? n.path + '/' + p : p }); }); n.files.push(f); });
  const all = n => [...n.files, ...Object.values(n.dirs).flatMap(all)];
  const out = [];
  const walk = (n, depth) => {
    Object.values(n.dirs).forEach(d => {
      const files = all(d), open = !collapsed.includes(d.path);
      out.push({ kind: 'dir', depth, name: d.name, open, files, checked: files.every(f => f.checked) ? true : files.some(f => f.checked) ? 'partial' : false });
      if (open) walk(d, depth + 1);
    });
    n.files.forEach(f => out.push({ kind: 'file', depth, f }));
  };
  walk(root, 0);
  return out;
}
function changesTable(c, x, y, w, h, lv, o = {}) {
  const t = T(), rh = 26, hh = 26, view = filesView(lv, o);
  c.group(view === 'tree' ? 'ChangesTree' : 'ChangesTable', () => {
    border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    // columns
    const wide = lv === 'xl' || lv === 'l';
    const cols = view === 'compact' ? [['', 30], ['Name', 0], ['St', 30]]
      : view === 'tree' ? (wide ? [['', 30], ['Name', 0], ['+ −', 70], ['St', 30]] : [['', 30], ['Name', 0], ['St', 30]])
      : lv === 'xl' ? [['', 30], ['Name', 0], ['Path', 130], ['Status', 70], ['+ −', 70], ['Size', 70]]
      : lv === 'l' ? [['', 30], ['Name', 0], ['Path', 120], ['Status', 60], ['+ −', 60]]
      : lv === 'm' ? [['', 30], ['Name', 0], ['Path', 110], ['St', 30]]
      : [['', 30], ['Name', 0], ['St', 30]];
    const fixed = cols.reduce((a, [, cw]) => a + cw, 0); cols.find(cc => cc[1] === 0)[1] = w - fixed;
    // header
    let cx = x; hairline(c, x, y + hh, w, { fo: 0.2 });
    cols.forEach(([name, cw], i) => {
      if (i > 0) vline(c, cx, y + 1, hh - 1, { fo: 0.1 });
      if (name === '') { const rows = o.rows || FILES; checkbox(c, { x: cx + 8, y: y + hh / 2 - 7, checked: rows.every(f => f.checked) ? true : rows.some(f => f.checked) ? 'partial' : false, id: 'CheckAll' }); }
      if (name) text(c, name === 'Name' || name === 'Path' ? cx + 10 : cx + cw / 2, y + hh / 2, name, { size: 10, weight: 700, fill: t.dim, anchor: name === 'Name' || name === 'Path' ? 'start' : 'middle' });
      cx += cw;
    });
    // rows
    let ry = y + hh + 1;
    const rows = o.rows || FILES;
    const lines = view === 'tree' ? fileTree(rows, o.collapsed || ['tests']) : rows.map(f => ({ kind: 'file', depth: 0, f }));
    lines.forEach((ln) => {
      if (ry + rh > y + h) return;
      const f = ln.f || { name: ln.name, checked: ln.checked, st: '' };
      const indent = ln.depth * 14;
      c.add(`<g id="${K.esc((ln.kind === 'dir' ? 'DirRow/' + (ln.open ? 'open' : 'collapsed') : 'Row/' + (f.selected ? 'selected' : 'normal')) + ' ' + f.name)}">`);
      if (f.selected) fillBox(c, x + 1, ry, w - 2, rh, 0.08);
      let cx = x;
      cols.forEach(([name, cw]) => {
        const mid = ry + rh / 2;
        if (name === '') checkbox(c, { x: cx + 8, y: mid - 7, checked: f.checked });
        else if (name === 'Name' && ln.kind === 'dir') {
          icon(c, ln.open ? 'chevron' : 'chevronR', cx + 6 + indent, mid - 6, 12, { fill: t.dim });
          icon(c, 'folderOutline', cx + 20 + indent, mid - 7, 14, { fill: t.dim });
          text(c, cx + 38 + indent, mid, ln.name, { fill: t.fg });
          if (!ln.open) text(c, cx + 38 + indent + tw(ln.name) + 8, mid, ln.files.length + (ln.files.length === 1 ? ' file' : ' files'), { fill: t.dim, size: SIZE.small });
        }
        else if (name === 'Name') {
          const col = f.selected ? t.accent : (f.st === '?' ? t.dim : statusColor(f.st));
          const nx = view === 'tree' ? cx + 8 + indent : cx + 10;
          text(c, nx, mid, f.name, { fill: col, weight: f.st === 'C' ? 700 : 400 });
          if (view === 'compact' && f.path) text(c, nx + tw(f.name) + 8, mid, f.path + '/', { fill: t.dim, size: SIZE.small });
        }
        else if (ln.kind === 'dir') { /* directories carry no status cells */ }
        else if (name === 'Path') text(c, cx + 10, mid, f.path, { fill: t.dim });
        else if (name === 'Status') text(c, cx + cw / 2, mid, K.statusName(f.st), { fill: statusColor(f.st), anchor: 'middle', size: SIZE.small });
        else if (name === 'St') statusPill(c, cx + (cw - 16) / 2, mid - 8, f.st);
        else if (name === '+ −') { if (f.add || f.del) { text(c, cx + cw / 2 - 2, mid, '+' + f.add, { fill: t.green, anchor: 'end', size: SIZE.small }); text(c, cx + cw / 2 + 2, mid, '−' + f.del, { fill: t.red, size: SIZE.small }); } }
        else if (name === 'Size') text(c, cx + cw - 10, mid, f.size, { fill: t.dim, anchor: 'end', size: SIZE.small });
        cx += cw;
      });
      c.add('</g>');
      ry += rh;
    });
    if (lines.length * rh > h - hh) scrollbar(c, x + w - 9, y + hh + 2, h - hh - 4, { ratio: 0.5 });
  });
}

// The Commit button says how many files it will commit ("Commit 5 files").
const commitLabel = rows => { const n = rows.filter(f => f.checked).length; return n === 1 ? 'Commit 1 file' : 'Commit ' + n + ' files'; };

// Bottom action bar of the commit page: options left, Commit right; always the same place.
function actionBar(c, x, y, w, lv, o = {}) {
  const t = T();
  c.group('ActionBar', () => {
    const label = o.amend ? 'Amend' : o.merging ? 'Commit merge' : commitLabel(o.rows || FILES);
    if (stacked(lv) || o.shallow) {
      let bx = x;
      bx += button(c, { x: bx, y, w: 28, icon: 'dots', id: 'Options' }) + 6;
      if (o.merging) { bx += button(c, { x: bx, y, variant: 'danger', label: 'Abort', id: 'AbortMerge' }) + 6; }
      button(c, { x: bx, y, w: x + w - bx, variant: 'primary', icon: 'commit', label: label, state: o.commitDisabled ? 'disabled' : 'normal', id: 'Commit' });
      // centre the label: overlay handled by button (left aligned); acceptable
    } else {
      let bx = x;
      // amend sits by the Commit it changes (the button reads "Amend" once it is on)
      bx += checkbox(c, { x: bx, y: y + 7, checked: !!o.amend, label: lv === 'm' ? 'Amend' : 'Amend last commit', id: 'AmendLastCommit' }) + 16;
      if (o.merging) { bx += button(c, { x: bx, y, variant: 'danger', icon: 'close', label: 'Abort merge', id: 'AbortMerge' }); }
      dimText(c, bx, y + 14, o.hint || '', { size: SIZE.small });
      const cw = measureButton({ icon: 'commit', label, px: 16 }) + 30;
      button(c, { x: x + w - cw, y, w: cw, variant: 'primary', icon: 'commit', label: label + '  ⏎', px: 16, state: o.commitDisabled ? 'disabled' : 'normal', id: 'Commit' });
    }
  });
}

// ------------------------------------------------------------ history page
function historyPage(c, x, y, w, h, lv, o = {}) {
  const t = T(); let cy = y;
  c.group('HistoryPage', () => {
    // filter row
    let fx = x;
    const abw = stacked(lv) ? 28 : measureButton({ icon: 'branch', label: 'All branches' });
    const rw = 28 + 6; // Refresh at the right end of the filter row
    const fw = w - abw - 8 - rw; const ph = fw < 300 ? (fw < 200 ? 'Filter' : 'Filter commits') : 'Filter by message, author or SHA'; field(c, { x: fx, y: cy, w: fw, icon: 'search', placeholder: ph });
    button(c, { x: x + w - abw - rw, y: cy, w: abw, icon: 'branch', label: stacked(lv) ? '' : 'All branches', state: o.allBranches ? 'selected' : 'normal', id: 'AllBranches' });
    if (rw) button(c, { x: x + w - 28, y: cy, w: 28, icon: 'refresh', id: 'Refresh' });
    cy += 36;
    const detailsH = o.shallow ? 0 : stacked(lv) ? 132 : 150;
    const filesH = o.shallow ? 0 : stacked(lv) ? 0 : 110;
    const tableH = h - (cy - y) - detailsH - filesH - (detailsH ? 10 : 0) - (filesH ? 10 : 0) - 22;
    commitsTable(c, x, cy, w, tableH, lv, o);
    cy += tableH + 10;
    if (detailsH) { commitDetails(c, x, cy, w, detailsH, lv, o); cy += detailsH + 10; }
    if (filesH) { changesTable(c, x, cy, w, filesH, lv, { rows: [{ name: 'Toolbar.cpp', path: 'src/ui', ext: '.cpp', size: '6.2 KiB', st: 'M', add: 4, del: 2, checked: true, selected: true }, { name: 'Toolbar.h', path: 'src/ui', ext: '.h', size: '1.9 KiB', st: 'M', add: 3, del: 0, checked: true }].map(r => ({ ...r, checked: undefined })) }); cy += filesH + 10; }
    dimText(c, x, cy + 6, '12 commits', { size: SIZE.small });
  });
}

function commitsTable(c, x, y, w, h, lv, o = {}) {
  const t = T(), rh = 28, hh = 26;
  c.group('CommitsTable', () => {
    border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    const cols = lv === 'xl' ? [['Graph', 40], ['Message', 0], ['Author', 90], ['Date', 130]]
      : lv === 'l' ? [['Graph', 40], ['Message', 0], ['Author', 90], ['Date', 100]]
      : lv === 'm' ? [['Graph', 36], ['Message', 0], ['Date', 90]]
      : [['Graph', 30], ['Message', 0]];
    const fixed = cols.reduce((a, [, cw]) => a + cw, 0); cols.find(cc => cc[1] === 0)[1] = w - fixed;
    let cx = x; hairline(c, x, y + hh, w, { fo: 0.2 });
    cols.forEach(([name, cw], i) => {
      if (i > 0) vline(c, cx, y + 1, hh - 1, { fo: 0.1 });
      if (name !== 'Graph') text(c, cx + 10, y + hh / 2, name, { size: 10, weight: 700, fill: t.dim });
      cx += cw;
    });
    let ry = y + hh + 1;
    const gx = x + cols[0][1] / 2;
    COMMITS.forEach((cm, i) => {
      if (ry + rh > y + h) return;
      c.add(`<g id="${K.esc('CommitRow/' + (i === 0 ? 'selected' : 'normal') + ' ' + cm.sha)}">`);
      if (i === 0) fillBox(c, x + 1, ry, w - 2, rh, 0.08);
      // graph
      const lx = gx - 6 + cm.lane * 12;
      rect(c, gx - 6 - 0.5, ry, 2, rh, { fill: t.accent, fo: 0.9 });
      if (cm.lane === 1 || (COMMITS[i + 1] && COMMITS[i + 1].lane === 1) || (COMMITS[i - 1] && COMMITS[i - 1].lane === 1)) rect(c, gx + 6 - 0.5, ry, 2, rh, { fill: t.magenta, fo: 0.9 });
      circle(c, lx, ry + rh / 2, 4, { fill: cm.lane ? t.magenta : t.accent, stroke: t.bg, sw: 1.5 });
      let mx = x + cols[0][1] + 10;
      cm.refs.forEach(([lb, kind]) => { if (lv === 'xs' && kind === 'remote') return; mx += refChip(c, mx, ry + rh / 2 - 8, lb, kind) + 6; });
      const avail = cols[1][1] - (mx - (x + cols[0][1])) - 10;
      let msg = cm.msg; const maxc = Math.floor(avail / (0.6 * 12)); if (msg.length > maxc) msg = msg.slice(0, Math.max(0, maxc - 1)) + '…';
      text(c, mx, ry + rh / 2, msg, { fill: i === 0 ? t.accent : t.fg });
      let ccx = x + cols[0][1] + cols[1][1];
      cols.slice(2).forEach(([name, cw]) => {
        const v = name === 'Author' ? cm.author : name === 'Date' ? (lv === 'm' ? cm.date.slice(0, 10) : cm.date) : cm.sha;
        text(c, ccx + 10, ry + rh / 2, v, { fill: t.dim, size: SIZE.small });
        ccx += cw;
      });
      c.add('</g>');
      ry += rh;
    });
    scrollbar(c, x + w - 9, y + hh + 2, h - hh - 4, { ratio: 0.3 });
  });
}

function commitDetails(c, x, y, w, h, lv, o = {}) {
  const t = T();
  c.group('CommitDetails', () => {
    border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    const cm = COMMITS[0]; let cy = y + 16;
    text(c, x + 12, cy, cm.msg, { weight: 700, size: SIZE.subtitle });
    cy += 22;
    let mx = x + 12;
    mx += text(c, mx, cy, cm.sha, { fill: t.accent, size: SIZE.small }) + 12;
    mx += text(c, mx, cy, cm.author + ' <andras@example.org>', { fill: t.dim, size: SIZE.small }) + 12;
    if (!stacked(lv)) mx += text(c, mx, cy, cm.date, { fill: t.dim, size: SIZE.small }) + 12;
    cy += 18;
    mx = x + 12; dimText(c, mx, cy, 'Parent 6bd79ab', { size: SIZE.small }); mx += tw('Parent 6bd79ab', SIZE.small) + 12;
    cm.refs.forEach(([lb, kind]) => { mx += refChip(c, mx, cy - 8, lb, kind) + 6; });
    cy += 22;
    const body = 'The toolbar folds its buttons in three steps now, and the whole window picks a layout from its width and height.';
    const maxc = Math.floor((w - 24) / 7.2); const words = body.split(' '); let line = ''; const lines = [];
    words.forEach(wd => { if ((line + ' ' + wd).trim().length > maxc) { lines.push(line); line = wd; } else line = (line ? line + ' ' : '') + wd; }); lines.push(line);
    const limit = y + h - (stacked(lv) ? 30 : 8);
    lines.forEach(l => { if (cy < limit) { text(c, x + 12, cy, l, { size: SIZE.body }); cy += 17; } });
    if (stacked(lv)) { button(c, { x: x + w - 8 - 90, y: y + h - 32, w: 90, h: 24, label: '2 files ›', variant: 'ghost', id: 'ShowFiles' }); }
    button(c, { x: x + w - 30, y: y + 6, w: 24, h: 24, variant: 'ghost', icon: 'copy', id: 'CopySha' });
  });
}

// ------------------------------------------------------------ diff pane
function diffPane(c, x, y, w, h, lv, o = {}) {
  const t = T();
  const split = o.split ?? (w >= 720);
  c.group('DiffPane', () => {
    // toolbar row
    let tx = x; const ty = y;
    const compact = w < 560;
    tx += button(c, { x: tx, y: ty, w: compact ? 28 : undefined, icon: 'up', label: compact ? '' : 'Prev', state: 'disabled', id: 'PrevChange' }) + 4;
    tx += button(c, { x: tx, y: ty, w: compact ? 28 : undefined, icon: 'down', label: compact ? '' : 'Next', id: 'NextChange' }) + 10;
    text(c, tx, ty + 14, compact ? '1/2' : 'Change 1 of 2', { fill: t.dim, size: SIZE.small }); tx += tw(compact ? '1/2' : 'Change 1 of 2', SIZE.small) + 10;
    if (!compact) { text(c, tx, ty + 14, '·', { fill: t.dim }); tx += 12; text(c, tx, ty + 14, 'Modified', { fill: t.blue, size: SIZE.small }); tx += tw('Modified', SIZE.small) + 8; text(c, tx, ty + 14, '+4', { fill: t.green, size: SIZE.small }); tx += 22; text(c, tx, ty + 14, '−2', { fill: t.red, size: SIZE.small }); }
    let rx = x + w;
    if (compact) { rx -= 28; button(c, { x: rx, y: ty, w: 28, icon: 'dots', id: 'DiffMore' }); }
    else {
      const labels = w >= 900;
      rx -= 28; button(c, { x: rx, y: ty, w: 28, icon: 'code', state: 'selected', id: 'Syntax' }); rx -= 4;
      rx -= 28; button(c, { x: rx, y: ty, w: 28, icon: 'pilcrow', id: 'Whitespace' }); rx -= 4;
      const vw = measureButton({ icon: split ? 'split' : 'unified', label: labels ? (split ? 'Split' : 'Unified') : '', chevron: true });
      rx -= vw; button(c, { x: rx, y: ty, icon: split ? 'split' : 'unified', label: labels ? (split ? 'Split' : 'Unified') : '', chevron: true, id: 'ViewMode' });
    }
    const by = y + 36, bh = h - 36;
    border(c, x, by, w, bh, { stroke: t.fg, so: 0.4 });
    const hh = 26, rh = 18, gutter = 34, mark = 18;
    const drawSide = (sx, sw, side, title, rows) => {
      // header
      fillBox(c, sx, by + 1, sw, hh, 0.04);
      text(c, sx + 10, by + 1 + hh / 2, 'src/ui/Toolbar.cpp', { weight: 700 });
      text(c, sx + sw - 10, by + 1 + hh / 2, title, { fill: t.dim, anchor: 'end', size: SIZE.small });
      hairline(c, sx, by + hh + 1, sw, { fo: 0.2 });
      let ry = by + hh + 4;
      const maxRows = Math.floor((bh - hh - 8) / rh);
      rows.slice(0, maxRows).forEach(r => {
        const seg = r[side];
        if (seg.k === 'filler') fillBox(c, sx + 1, ry, sw - 2, rh, 0.05);
        else if (seg.k === 'del') fillBox(c, sx + 1, ry, sw - 2, rh, 0.16, { fill: t.red });
        else if (seg.k === 'add') fillBox(c, sx + 1, ry, sw - 2, rh, 0.16, { fill: t.green });
        if (seg.k === 'del' || seg.k === 'add') icon(c, seg.k === 'del' ? 'minus' : 'plus', sx + 3, ry + 2, 14, { fill: seg.k === 'del' ? t.red : t.green });
        if (seg.n !== undefined) text(c, sx + mark + gutter - 6, ry + rh / 2, String(seg.n), { fill: t.dim, anchor: 'end', size: SIZE.small });
        if (seg.t !== undefined) {
          let cx = sx + mark + gutter + 8; const maxc = Math.floor((sw - (mark + gutter + 16)) / 7.2);
          (seg.hl || []).forEach(([a, b]) => { if (a < maxc) fillBox(c, cx + a * 7.2, ry + 1, Math.min(b, maxc) * 7.2 - a * 7.2, rh - 2, 0.35, { fill: seg.k === 'del' ? t.red : t.green }); });
          let used = 0;
          tokens(seg.t).forEach(tk => {
            if (used >= maxc) return;
            const s = tk.s.slice(0, maxc - used);
            text(c, cx + used * 7.2, ry + rh / 2, s, { fill: tk.fill });
            used += s.length;
          });
        }
        ry += rh;
      });
      // change marks at the right edge
      const marks = rows.slice(0, maxRows).map((r, i) => [i, r[side].k]).filter(([, k]) => k === 'del' || k === 'add');
      marks.forEach(([i, k]) => rect(c, sx + sw - 6, by + hh + 4 + i * rh, 3, rh, { fill: k === 'del' ? t.red : t.green }));
    };
    if (split) {
      const half = Math.floor(w / 2);
      drawSide(x, half, 'l', 'HEAD', DIFF);
      vline(c, x + half, by, bh, { fo: 0.4 });
      drawSide(x + half + 1, w - half - 1, 'r', 'Working tree', DIFF);
    } else {
      // unified: dels then adds
      const rows = [];
      DIFF.forEach(r => { if (r.l.k === 'ctx') rows.push({ u: { ...r.l, n2: r.r.n } }); else { if (r.l.k === 'del') rows.push({ u: r.l }); if (r.r.k === 'add') rows.push({ u: r.r }); } });
      drawSide(x, w, 'u', 'HEAD → Working tree', rows);
    }
    scrollbar(c, x + w - 16, by + hh + 4, bh - hh - 8, { ratio: 0.6 });
  });
}

// ------------------------------------------------------------ mini rail
function miniRail(c, x, y, h, o = {}) {
  const t = T(), s = 40, gap = 6; let ty = y;
  c.group('MiniRail', () => {
    FILES.forEach((f, i) => {
      c.add(`<g id="${K.esc('Tile/' + (f.selected ? 'selected' : f.checked ? 'checked' : 'unchecked') + ' ' + f.name)}">`);
      const op = f.checked ? 1 : 0.45;
      fillBox(c, x, ty, s, s, f.selected ? 0.18 : 0.04);
      border(c, x, ty, s, s, { stroke: f.selected ? t.accent : t.fg, so: f.selected ? 1 : 0.4 });
      text(c, x + s / 2, ty + s / 2 + 2, f.ext.slice(1), { size: 10, weight: 700, fill: f.selected ? t.accent : statusColor(f.st), anchor: 'middle', opacity: op });
      const col = statusColor(f.st); rect(c, x + s - 13, ty + 1, 12, 12, { fill: col, fo: f.checked ? 1 : 0.5 });
      text(c, x + s - 7, ty + 7, f.st, { size: 8, weight: 700, fill: t.bg, anchor: 'middle' });
      text(c, x + 4, ty + s - 6, String(i + 1), { size: 8, fill: t.dim });
      c.add('</g>');
      ty += s + gap;
    });
    // the commit tile: Commit stays reachable while the diff has the window (Ctrl+Enter opens the popover)
    if (o.commitTile !== false && y + h - s >= ty + 8) {
      const cy = y + h - s, n = FILES.filter(f => f.checked).length;
      hairline(c, x, cy - 8, s, { fo: 0.2 });
      c.group('Tile/commit', () => {
        fillBox(c, x, cy, s, s, 0.08, { fill: t.accent }); border(c, x, cy, s, s, { stroke: t.accent, so: 1 });
        icon(c, 'commit', x + (s - 16) / 2, cy + (s - 16) / 2 + 2, 16, { fill: t.accent });
        rect(c, x + s - 13, cy + 1, 12, 12, { fill: t.accent }); text(c, x + s - 7, cy + 7, String(n), { size: 8, weight: 700, fill: t.bg, anchor: 'middle' });
      });
      (c.anchors = c.anchors || {}).CommitTile = { x, y: cy, w: s, h: s }; // for the popover; not a control, so not in the manifest
    }
  });
  return s;
}

// ------------------------------------------------------------ commit popover (Mini layout, Diff tab)
// Anchored to the rail's commit tile: the message box, Amend and Commit, nothing the action bar would not show.
function commitPopover(c, o) {
  const t = T(), { x, w } = o, pad = 10, inner = w - pad * 2, mh = 84;
  const h = pad + 22 + mh + 10 + 16 + 10 + 1 + 12 + 28 + pad;
  const y = o.bottom !== undefined ? o.bottom - h : o.y;
  c.group(o.id || 'CommitPopover', () => {
    rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    let cy = y + pad;
    sectionLabel(c, x + pad, cy + 6, 'Message');
    button(c, { x: x + w - pad - 24, y: cy - 6, w: 24, h: 24, variant: 'ghost', icon: 'cog', id: 'AgentSettings' });
    cy += 22;
    c.group('MessageBox', () => {
      fillBox(c, x + pad, cy, inner, mh, 0.04); border(c, x + pad, cy, inner, mh, { stroke: t.fg, so: 0.4 });
      if (o.message) { text(c, x + pad + 8, cy + 14, o.message, { fill: t.fg }); (o.messageBody || []).forEach((l, i) => text(c, x + pad + 8, cy + 14 + 18 * (i + 1), l, { fill: t.fg })); }
      else text(c, x + pad + 8, cy + 14, 'Commit message', { fill: t.dim });
      button(c, { x: x + w - pad - 26, y: cy + 2, w: 24, h: 24, variant: 'ghost', icon: 'sparkle', id: 'Generate' });
    });
    cy += mh + 10;
    const n = FILES.filter(f => f.checked).length;
    dimText(c, x + pad, cy + 8, n + ' / ' + FILES.length + ' files selected · Space on a tile toggles it', { size: SIZE.small, id: 'SelectedCount' });
    cy += 16 + 10;
    hairline(c, x + pad, cy, inner, { fo: 0.15 }); cy += 12;
    checkbox(c, { x: x + pad, y: cy + 7, checked: !!o.amend, label: 'Amend last commit', id: 'AmendLastCommit' });
    const label = o.amend ? 'Amend' : commitLabel(FILES), cw = measureButton({ icon: 'commit', label, px: 16 }) + 30;
    button(c, { x: x + w - pad - cw, y: cy, w: cw, variant: 'primary', icon: 'commit', label: label + '  ⏎', px: 16, id: 'Commit' });
  });
  return h;
}

// ------------------------------------------------------------ footer
function footer(c, W, H, lv, o = {}) {
  const t = T(), h = 28, y = H - h;
  c.group('Footer', () => {
    hairline(c, 0, y, W, { fo: 0.12 });
    const st = o.status || (lv === 'xs' ? '~/Projects/omagit' : 'Fetched origin 2 min ago · ~/Projects/omagit');
    if (o.busy) { icon(c, 'loader', 12, y + 7, 14, { fill: t.accent }); text(c, 32, y + 14, st, { fill: t.dim, size: SIZE.small }); }
    else text(c, 12, y + 14, st, { fill: t.dim, size: SIZE.small });
    button(c, { x: W - 12 - 24, y: y + 2, w: 24, h: 24, variant: 'ghost', icon: 'keyboard', id: 'Keybindings' });
  });
  return h;
}

// ------------------------------------------------------------ menus / cards
function menuCard(c, o) {
  const t = T(), { x, y, w } = o, pad = 6, rh = 30;
  let total = pad * 2; o.items.forEach(it => total += it.type === 'section' ? 22 : it.type === 'sep' ? 9 : it.type === 'search' ? 34 : rh);
  const h = o.h || total;
  c.group(o.id || 'MenuCard', () => {
    rect(c, x, y, w, h, { fill: t.bg });
    border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    let cy = y + pad;
    o.items.forEach(it => {
      if (it.type === 'search') { field(c, { x: x + pad, y: cy, w: w - pad * 2, icon: 'search', placeholder: it.label, state: 'prompt', value: it.value }); hairline(c, x + pad + 4, cy + 31, w - pad * 2 - 8); cy += 34; return; }
      if (it.type === 'section') { sectionLabel(c, x + pad + 8, cy + 13, it.label); cy += 22; return; }
      if (it.type === 'sep') { hairline(c, x + pad + 4, cy + 4, w - pad * 2 - 8); cy += 9; return; }
      c.add(`<g id="${K.esc('MenuRow/' + (it.hover ? 'hover' : 'normal') + ' ' + it.label)}">`);
      if (it.hover) fillBox(c, x + pad, cy, w - pad * 2, rh, 0.08);
      let ix = x + pad + 10;
      if (it.icon) { icon(c, it.icon, ix, cy + 8, 14, { fill: it.hover ? t.accent : t.fg, opacity: it.disabled ? 0.45 : 1 }); ix += 22; }
      text(c, ix, cy + rh / 2, it.label, { fill: it.hover ? t.accent : t.fg, opacity: it.disabled ? 0.45 : 1 });
      if (it.hint) text(c, x + w - pad - 10 - (it.checked ? 20 : 0), cy + rh / 2, it.hint, { fill: t.dim, anchor: 'end', size: SIZE.small });
      if (it.checked) icon(c, 'check', x + w - pad - 24, cy + 8, 14, { fill: t.accent });
      c.add('</g>');
      cy += rh;
    });
  });
  return h;
}

function dialogCard(c, o) {
  const t = T(), { x, y, w, h } = o;
  c.group(o.id || 'Dialog', () => {
    rect(c, x, y, w, h, { fill: t.bg });
    border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    text(c, x + 20, y + 30, o.title, { size: SIZE.heading, weight: 500 });
    if (o.subtitle) text(c, x + 20, y + 52, o.subtitle, { fill: t.dim, size: SIZE.small });
    button(c, { x: x + w - 20 - 24, y: y + 18, w: 24, h: 24, variant: 'ghost', icon: 'close', id: 'Close' });
  });
}

function mergeDialog(c, x, y, w, o = {}) {
  const t = T(), h = o.h || 300, narrow = w < 520;
  dialogCard(c, { x, y, w, h, title: 'Merge', subtitle: 'Worked out on the trees alone — nothing touches the working tree yet.', id: 'MergeDialog' });
  let cy = y + 74; const px = 20;
  const inner = w - px * 2;
  if (narrow) {
    sectionLabel(c, x + px, cy, 'Merge'); cy += 14;
    button(c, { x: x + px, y: cy, w: inner, h: 34, icon: 'branch', label: 'feature/askpass', chevron: true, weight: 700, id: 'BranchPicker/source' }); cy += 40;
    button(c, { x: x + px, y: cy, w: 34, h: 28, variant: 'ghost', icon: 'swap', id: 'Swap' });
    text(c, x + px + 44, cy + 14, 'INTO', { size: 10, weight: 700, fill: t.dim, ls: 0.8 }); cy += 34;
    button(c, { x: x + px, y: cy, w: inner, h: 34, icon: 'branch', label: 'main', chevron: true, weight: 700, id: 'BranchPicker/target' }); cy += 46;
  } else {
    const pw = Math.floor((inner - 46) / 2);
    sectionLabel(c, x + px, cy, 'Merge'); sectionLabel(c, x + px + pw + 46, cy, 'Into'); cy += 14;
    button(c, { x: x + px, y: cy, w: pw, h: 34, icon: 'branch', label: 'feature/askpass', chevron: true, weight: 700, id: 'BranchPicker/source' });
    button(c, { x: x + px + pw + 6, y: cy, w: 34, h: 34, icon: 'swap', id: 'Swap' });
    button(c, { x: x + px + pw + 46, y: cy, w: pw, h: 34, icon: 'branch', label: 'main', chevron: true, weight: 700, id: 'BranchPicker/target' });
    cy += 46;
  }
  // verdict card
  const vh = narrow ? 96 : 84;
  c.group('VerdictCard', () => {
    fillBox(c, x + px, cy, inner, vh, 0.04); border(c, x + px, cy, inner, vh, { stroke: t.fg, so: 0.4 });
    icon(c, 'checkCircle', x + px + 12, cy + 12, 16, { fill: t.green });
    text(c, x + px + 36, cy + 20, 'Clean merge — no conflicts.', { weight: 700, fill: t.green });
    text(c, x + px + 36, cy + 40, narrow ? '2 commits · 3 files' : 'Creates a merge commit bringing 2 commits and 3 changed files into main.', { fill: t.dim, size: SIZE.small });
    if (narrow) text(c, x + px + 36, cy + 56, '+41 −7 lines', { fill: t.dim, size: SIZE.small });
    else text(c, x + px + 36, cy + 58, '+41 −7 lines', { fill: t.dim, size: SIZE.small });
  });
  cy += vh + 14;
  checkbox(c, { x: x + px, y: cy, checked: false, label: 'Always create a merge commit' }); cy += 30;
  const by = y + h - 20 - 28;
  const mw = measureButton({ icon: 'merge', label: 'Merge', px: 16 });
  button(c, { x: x + w - px - mw, y: by, icon: 'merge', label: 'Merge', variant: 'primary', px: 16, id: 'MergeButton' });
  button(c, { x: x + w - px - mw - 8 - measureButton({ label: 'Cancel' }), y: by, label: 'Cancel', id: 'Cancel' });
}

// The cog's popover: which agent writes the commit message, with which model
// and reasoning level. Three dependent choices top to bottom (agent → model →
// level), the exact command it will run, and a way to generate right away.
// o.agent: 'claude' | 'codex' | 'none'; o.model / o.effort: chosen ids ('' = default).
const AGENTS = {
  claude: { name: 'Claude Code', bin: 'claude', models: [['', 'Default', 'whatever claude uses'], ['fable', 'Fable', 'claude-fable-5-1'], ['opus', 'Opus', 'claude-opus-5'], ['sonnet', 'Sonnet', 'claude-sonnet-5']],
    levels: ['low', 'medium', 'high', 'xhigh', 'max'], modelFlag: '--model', effortFlag: '--effort' },
  codex: { name: 'Codex', bin: 'codex', models: [['', 'Default', 'whatever codex uses'], ['gpt-6-astra', 'GPT-6 Astra', 'gpt-6-astra'], ['gpt-5.5', 'GPT-5.5', 'gpt-5.5'], ['gpt-5.5-mini', 'GPT-5.5 mini', 'gpt-5.5-mini']],
    levels: ['low', 'medium', 'high', 'xhigh', 'max', 'ultra'], modelFlag: '-m', effortFlag: '-c model_reasoning_effort=' },
};
function agentPopover(c, o) {
  const t = T(), { x, y, w } = o, pad = 10, inner = w - pad * 2, rh = 28;
  const agent = o.agent || 'claude', A = AGENTS[agent], model = o.model ?? 'opus', effort = o.effort ?? 'high';
  const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);
  const levels = A ? ['', ...A.levels] : [];
  // height: sections + rows + track + footer
  const modelRows = A ? A.models.length : 0;
  const h = A ? pad + 22 + 28 + 16 + 22 + modelRows * rh + 6 + 28 + 16 + 22 + 44 + 12 + 1 + 12 + 28 + pad : 184;
  c.group(o.id || 'AgentPopover', () => {
    rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    let cy = y + pad;
    if (!A) { // nothing installed: say what is missing and how to get it, copyable
      icon(c, 'robot', x + pad + 2, cy + 2, 20, { fill: t.dim });
      text(c, x + pad + 32, cy + 12, 'No coding agent installed', { weight: 700 });
      text(c, x + pad + 32, cy + 30, 'Claude Code or Codex writes it for you.', { fill: t.dim, size: SIZE.small });
      cy += 52;
      sectionLabel(c, x + pad, cy + 6, 'Install one'); cy += 20;
      const cmds = ['omarchy default agent claude', 'omarchy default agent codex'];
      cmds.forEach(cmd => {
        c.group('CommandRow ' + cmd, () => {
          fillBox(c, x + pad, cy, inner, 30, 0.04); border(c, x + pad, cy, inner, 30, { stroke: t.fg, so: 0.25 });
          text(c, x + pad + 10, cy + 15, '$ ' + cmd, { fill: t.fg });
          button(c, { x: x + w - pad - 3 - 24, y: cy + 3, w: 24, h: 24, variant: 'ghost', icon: 'copy', id: 'Copy' });
        });
        cy += 36;
      });
      text(c, x + pad, cy + 10, 'Reopen this menu once one is installed.', { fill: t.dim, size: SIZE.small });
      return;
    }
    // AGENT: only installed ones, the Omarchy default named
    sectionLabel(c, x + pad, cy + 6, 'Agent');
    dimText(c, x + w - pad, cy + 6, 'claude is the Omarchy default', { anchor: 'end', size: SIZE.caption });
    cy += 22;
    segmented(c, { x: x + pad, y: cy, w: inner, stretch: true, id: 'AgentPicker', items: [
      { icon: 'robot', label: 'Claude Code', selected: agent === 'claude' }, { icon: 'robot', label: 'Codex', selected: agent === 'codex' }] });
    cy += 28 + 16;
    // MODEL: the names the CLI uses, the exact id at the right, tick on the chosen one
    sectionLabel(c, x + pad, cy + 6, 'Model');
    dimText(c, x + w - pad, cy + 6, 'from ' + (agent === 'claude' ? 'claude --help' : 'codex debug models'), { anchor: 'end', size: SIZE.caption });
    cy += 22;
    A.models.forEach(([id, name, detail], i) => {
      const sel = id === model, hover = o.hover !== undefined ? o.hover === i : false;
      c.group('ModelRow/' + (sel ? 'selected' : hover ? 'hover' : 'normal') + ' ' + name, () => {
        if (hover) fillBox(c, x + pad, cy, inner, rh, 0.08);
        text(c, x + pad + 10, cy + rh / 2, name, { fill: sel || hover ? t.accent : t.fg, weight: sel ? 700 : 400 });
        text(c, x + w - pad - 30, cy + rh / 2, detail, { fill: t.dim, size: SIZE.small, anchor: 'end' });
        if (sel) icon(c, 'check', x + w - pad - 24, cy + 7, 14, { fill: t.accent });
      });
      cy += rh;
    });
    cy += 6;
    // a custom model name is the exception: a text button, the field only once asked for
    if (o.otherOpen) field(c, { x: x + pad, y: cy, w: inner, state: 'focus', value: o.otherValue || '', placeholder: 'Model name, as ' + A.bin + ' ' + A.modelFlag + ' takes it', id: 'OtherModel' });
    else button(c, { x: x + pad, y: cy, variant: 'ghost', label: 'Other model…', id: 'OtherModelButton' });
    cy += 28 + 16;
    // REASONING: an ordered scale, so a track with stops instead of a list
    sectionLabel(c, x + pad, cy + 6, 'Reasoning');
    dimText(c, x + w - pad, cy + 6, 'more thinking, slower answer', { anchor: 'end', size: SIZE.caption });
    cy += 22;
    c.group('LevelTrack', () => {
      const n = levels.length, step = (inner - 56) / (n - 1), tx = x + pad + 28, ty = cy + 12;
      const si = Math.max(0, levels.indexOf(effort));
      hairline(c, tx, ty, inner - 56, { fo: 0.3 });
      if (si > 0) rect(c, tx, ty - 0.5, step * si, 2, { fill: t.accent, fo: 0.7 });
      levels.forEach((lv, i) => {
        const cx = tx + step * i, sel = i === si;
        c.group('Stop/' + (sel ? 'selected' : 'normal') + ' ' + (lv || 'default'), () => {
          if (sel) { circle(c, cx, ty, 7, { fill: t.accent, fo: 0.25 }); circle(c, cx, ty, 4.5, { fill: t.accent }); }
          else { circle(c, cx, ty, 4, { fill: t.bg }); circle(c, cx, ty, 4, { fill: 'none', stroke: t.fg, so: i < si ? 0.9 : 0.5, sw: 1.5 }); }
          text(c, cx, ty + 22, lv ? cap(lv) : 'Default', { size: SIZE.caption, weight: sel ? 700 : 400, fill: sel ? t.accent : t.dim, anchor: 'middle' });
        });
      });
    });
    cy += 44 + 12;
    hairline(c, x + pad, cy, inner, { fo: 0.15 }); cy += 12;
    button(c, { x: x + pad, y: cy, w: inner, icon: 'sparkle', label: 'Generate now  Ctrl+G', variant: 'primary', px: 12, id: 'GenerateNow' });
  });
  return h;
}

function keybindingsPanel(c, x, y, w, h) {
  const t = T();
  c.group('KeybindingsPanel', () => {
    rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    text(c, x + 18, y + 18 + 12, 'Omagit keybindings…', { size: SIZE.title, fill: t.dim });
    let cy = y + 18 + 34 + 6;
    const rows = [['CTRL + K', 'Keybindings', true], ['CTRL + 1', 'Changes'], ['CTRL + 2', 'History'], ['CTRL + 3', 'Diff'], ['CTRL + ENTER', 'Commit'], ['CTRL + G', 'Generate message'], ['CTRL + P', 'Pull'], ['CTRL SHIFT + P', 'Push'], ['CTRL + F', 'Fetch'], ['CTRL SHIFT + M', 'Merge']];
    const rh = 40;
    rows.forEach(([k, v, hot]) => {
      if (cy + rh > y + h - 18) return;
      if (hot) fillBox(c, x + 18, cy, w - 36, rh, 0.08);
      text(c, x + 18 + 14, cy + rh / 2, k, { size: SIZE.subtitle, weight: 500, fill: hot ? t.accent : t.fg });
      icon(c, 'chevronR', x + 18 + 14 + 150, cy + rh / 2 - 7, 14, { fill: t.dim });
      text(c, x + 18 + 14 + 170, cy + rh / 2, v, { size: SIZE.subtitle, weight: 500, fill: hot ? t.accent : t.fg });
      cy += rh + 2;
    });
  });
}

// ------------------------------------------------------------ screen composer
function screen(o) {
  const { W, H } = o; const lv = o.level || levelFor(W); const shallow = H < 560;
  const c = new K.Canvas(o.id, W, H); const t = T();
  rect(c, 0, 0, W, H, { fill: t.bg, id: 'window' });
  const top = topBar(c, W, lv, o);
  const footH = shallow ? 0 : footer(c, W, H, lv, o);
  const m = 12, bodyY = top + 10, bodyH = H - bodyY - footH - 10;
  const page = o.page || 'changes';
  if (stacked(lv)) {
    const nx = m, nw = W - m * 2;
    const py = bodyY, ph = bodyH;
    if (page === 'changes') changesPage(c, nx, py, nw, ph, lv, { ...o, shallow });
    else if (page === 'history') historyPage(c, nx, py, nw, ph, lv, { ...o, shallow });
    else { // diff tab: rail + diff
      const rw = miniRail(c, nx, py, ph);
      diffPane(c, nx + rw + 10, py, nw - rw - 10, ph, lv, { split: false });
    }
  } else {
    const leftW = o.leftW || (lv === 'xl' ? 560 : lv === 'l' ? 400 : 340);
    if (o.mini) {
      const rw = miniRail(c, m, bodyY, bodyH);
      diffPane(c, m + rw + 12, bodyY, W - m * 2 - rw - 12, bodyH, lv, o);
    } else if (o.diffHidden) {
      if (page === 'changes') changesPage(c, m, bodyY, W - m * 2, bodyH, lv, { ...o, shallow });
      else historyPage(c, m, bodyY, W - m * 2, bodyH, lv, { ...o, shallow });
    } else {
      if (page === 'changes') changesPage(c, m, bodyY, leftW, bodyH, lv, { ...o, shallow });
      else historyPage(c, m, bodyY, leftW, bodyH, lv, { ...o, shallow });
      // splitter handle
      rect(c, m + leftW + 5, bodyY + bodyH / 2 - 16, 2, 32, { fill: t.fg, fo: 0.2, id: 'SplitterHandle' });
      diffPane(c, m + leftW + 12, bodyY, W - m * 2 - leftW - 12, bodyH, lv, o);
    }
  }
  // overlays
  if (o.overlay === 'branch') {
    const bx = 12 + measureButton({ icon: 'folderOpen', label: stacked(lv) ? '' : 'omagit', chevron: !stacked(lv) }) + 4, by = 40 + 4;
    menuCard(c, { x: bx, y: by, w: Math.min(300, W - 24), id: 'BranchMenu', items: [
      { type: 'search', label: 'Search branches…' }, { type: 'section', label: 'Local' },
      { label: 'main', icon: 'branch', checked: true }, { label: 'feature/askpass', icon: 'branch', hover: true, hint: '2 days ago' }, { label: 'feature/tiling', icon: 'branch', hint: 'today' },
      { type: 'section', label: 'Remote' }, { label: 'origin/main', icon: 'cloud' }, { label: 'origin/feature/askpass', icon: 'cloud' }] });
  }
  if (o.overlay === 'repo') {
    menuCard(c, { x: 12, y: 44, w: 300, id: 'RepoMenu', items: [
      { type: 'search', label: 'Search repositories…' }, { type: 'section', label: 'Recent' },
      { label: 'omagit', icon: 'folder', checked: true, hint: '~/Projects' }, { label: 'feelinmyskin-mobile', icon: 'folder', hint: '~/Projects' }, { label: 'dotfiles', icon: 'folder', hint: '~' },
      { type: 'sep' }, { label: 'Open…', icon: 'folderOpen', hint: 'Ctrl+O' }, { label: 'Clone…', icon: 'fetch', hint: 'Ctrl+Shift+O' }] });
  }
  if (o.overlay === 'sync') {
    menuCard(c, { x: W - 12 - 260, y: 44, w: 260, id: 'SyncMenu', items: [
      { label: 'Pull', icon: 'pull', hint: '2 behind · Ctrl+P', hover: true }, { label: 'Push', icon: 'push', hint: '1 ahead · Ctrl+Shift+P' }, { label: 'Fetch', icon: 'fetch', hint: 'Ctrl+F' },
      { type: 'sep' }, { label: 'Merge…', icon: 'merge', hint: 'Ctrl+Shift+M' }] });
  }
  if (o.overlay === 'more') {
    menuCard(c, { x: W - 12 - 240, y: 44, w: 240, id: 'MoreMenu', items: [
      { label: 'Fetch', icon: 'fetch', hint: 'Ctrl+F' }, { label: 'Merge…', icon: 'merge', hint: 'Ctrl+Shift+M' }, { type: 'sep' },
      { label: 'Refresh', icon: 'refresh', hint: 'F5' }, { label: 'Open repository…', icon: 'folderOpen', hint: 'Ctrl+O' }, { label: 'Clone…', icon: 'fetch' }, { type: 'sep' },
      { label: 'Keybindings', icon: 'keyboard', hint: 'Ctrl+K' }] });
  }
  if (o.overlay === 'options') {
    menuCard(c, { x: 12, y: H - 28 - 12 - 44 - 150, w: 240, id: 'OptionsMenu', items: [
      { label: 'Select all', icon: 'check', hint: 'Ctrl+Shift+Space' }, { label: 'Show unversioned files', icon: 'checkCircle', checked: true }, { label: 'Amend last commit', icon: 'undo', hint: 'Ctrl+Shift+A' }, { type: 'sep' }, { label: 'Generate message', icon: 'sparkle', hint: 'Ctrl+G' }] });
  }
  if (o.overlay === 'merge') {
    const dw = Math.min(640, W - 24), dh = dw < 520 ? 384 : 300;
    fillBox(c, 0, 0, W, H, 0.5, { fill: t.bg, id: 'Scrim' });
    mergeDialog(c, Math.round((W - dw) / 2), Math.round((H - dh) / 2), dw, { h: dh });
  }
  if (o.overlay === 'keys') {
    const kw = Math.min(800, W - 24), kh = Math.min(500, H - 24);
    fillBox(c, 0, 0, W, H, 0.5, { fill: t.bg, id: 'Scrim' });
    keybindingsPanel(c, Math.round((W - kw) / 2), Math.round((H - kh) / 2), kw, kh);
  }
  if (o.overlay === 'commit') {
    const tile = c.anchors.CommitTile;
    const pw = Math.min(360, W - 24 - tile.w - 8);
    commitPopover(c, { x: tile.x + tile.w + 8, bottom: tile.y + tile.h, w: pw, message: o.message, messageBody: o.messageBody, amend: o.amend });
  }
  if (o.overlay === 'agent') {
    const cog = c.rec.find(r => r.id === 'AgentSettings');
    const pw = Math.min(360, W - 24);
    agentPopover(c, { x: Math.max(12, cog.x + cog.w - pw), y: cog.y + cog.h + 6, w: pw, ...(o.agent || {}) });
  }
  return c;
}

module.exports = { screen, agentPopover, commitPopover, levelFor, topBar, changesPage, changesTable, actionBar, historyPage, commitsTable, commitDetails, diffPane, miniRail, footer, menuCard, dialogCard, mergeDialog, keybindingsPanel, FILES, COMMITS, DIFF, tokens };
