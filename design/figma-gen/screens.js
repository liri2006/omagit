// Screen composers: top bar (with the page tabs), pages, diff pane, action bar, footer, menus, dialogs.
// Sizes and gaps come from kit.js's grid (BOX, PAD, GAP, BAR) and from the
// window's density (margin, block — see density()).
const K = require('./kit.js');
const { rect, border, fillBox, hairline, vline, text, icon, circle, button, segmented, field, checkbox, sectionLabel, dimText, refChip,
  statusColor, statusPill, scrollbar, tw, SIZE, measureButton, measureSegmented, BOX, PAD, GAP, BAR, DENSITY } = K;
const T = () => K.theme();

// ------------------------------------------------------------ breakpoints
// Width classes in px at base font 12 (everything scales with base/12).
function levelFor(W) { return W >= 1400 ? 'xl' : W >= 1000 ? 'l' : W >= 700 ? 'm' : W >= 480 ? 's' : 'xs'; }
const wide = lv => lv === 'xl' || lv === 'l';
const stacked = lv => lv === 's' || lv === 'xs';
// Height classes: shallow windows lose the footer and the tall message box,
// tall ones get the roomier vertical spacing.
const SHALLOW = 560, TALL = 1000;
// Spacing between things steps with the window, one step per class: the side
// margins and pane gaps follow its width (XL comfortable, L/M regular, S/XS
// compact), the block gap its height (tall, normal, shallow).
function density(W, H) {
  const lv = levelFor(W);
  const x = lv === 'xl' ? 'comfortable' : stacked(lv) ? 'compact' : 'regular';
  const y = H >= TALL ? 'comfortable' : H < SHALLOW ? 'compact' : 'regular';
  return { x, y, margin: DENSITY[x].margin, block: DENSITY[y].block };
}
const REGULAR = { x: 'regular', y: 'regular', ...DENSITY.regular }; // standalone patterns
const TOP_BAR = BAR + BOX.control + BAR; // 44, its hairline the last pixel row
const SEP = 12; // a hairline between parts of a card stands 12 below the part above it and 12 above the next

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
  { sha: '14d6d14', msg: 'Add repository cloning with GitHub browser integration', author: 'Andras', date: '2026-09-17 12:10', refs: [['v0.4', 'tag']], lane: 0,
    body: 'Clone from a URL or from your GitHub repositories; the dialog signs in through gh when it has to.' },
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
// A filter's matches out of the whole history ("fix"): no graph, several
// authors, years apart — the search is not limited to the loaded commits.
const FIX_MATCHES = [
  { sha: 'e41c2a7', msg: 'Fix the diff scroll after a refresh', author: 'Andras', date: '2026-09-22 16:03', refs: [['main', 'head']],
    body: 'The diff kept its document but not its scroll position when a refresh re-read the same file, so it jumped back to the first hunk.' },
  { sha: '8b0f3d1', msg: 'Fix askpass prompt showing twice for one remote', author: 'Andras', date: '2026-09-19 11:47', refs: [] },
  { sha: 'c93e5a0', msg: 'Fix crash when the repository has no commits', author: 'Halina', date: '2026-08-30 09:12', refs: [['v0.3.1', 'tag']] },
  { sha: '51d7ee4', msg: 'Prefix remote branch names in the merge dialog', author: 'Istvan', date: '2026-08-11 13:26', refs: [] },
  { sha: '2a6f190', msg: 'Fix tree view losing folded folders on sort', author: 'Andras', date: '2026-07-14 18:20', refs: [] },
  { sha: 'f07b3c8', msg: 'Hotfix: keep the window geometry on Hyprland', author: 'Halina', date: '2026-06-02 08:55', refs: [] },
  { sha: '9e24d6b', msg: 'Fix syntax colours for raw string literals', author: 'Istvan', date: '2026-04-19 21:10', refs: [] },
  { sha: '06ac5f2', msg: 'Fix off-by-one in the lane layout for octopus merges', author: 'Andras', date: '2025-12-03 14:32', refs: [] },
  { sha: '7d3b9a4', msg: 'Fix typo in README install steps', author: 'Halina', date: '2025-09-27 10:05', refs: [] },
  { sha: 'b85e017', msg: 'Fix fetch backoff never resetting', author: 'Andras', date: '2025-05-11 19:48', refs: [] },
  { sha: '3c1f6e9', msg: 'Fix Nautilus extension on paths with spaces', author: 'Istvan', date: '2024-11-20 12:00', refs: [] },
  { sha: 'd2e84b0', msg: 'Fix the status bar clipping at base 18', author: 'Andras', date: '2024-02-08 17:25', refs: [] },
  { sha: '64fa1c3', msg: 'Fix build with Qt 6.5', author: 'Halina', date: '2023-06-30 09:40', refs: [] },
  { sha: 'a0e5d77', msg: 'Fix merge-tree verdict for renamed files', author: 'Istvan', date: '2023-03-14 15:05', refs: [] },
  { sha: '5f9c2b1', msg: 'Fix blame view for files with CRLF endings', author: 'Andras', date: '2022-10-02 11:30', refs: [] },
  { sha: 'ce37a48', msg: 'Fix keyboard focus after closing the branch menu', author: 'Halina', date: '2022-05-21 20:14', refs: [] },
  { sha: '18b4e90', msg: 'Fix untracked files missing from the tree view', author: 'Andras', date: '2021-12-09 09:52', refs: [] },
  { sha: '9a7d3f2', msg: 'Fix empty commit message check', author: 'Istvan', date: '2021-06-17 16:40', refs: [] },
  { sha: '4e0b6c5', msg: 'Fix ahead/behind badge after a force push', author: 'Andras', date: '2020-11-24 13:08', refs: [] },
  { sha: 'f3c81a9', msg: 'Fix the initial window size on small screens', author: 'Halina', date: '2020-04-03 10:21', refs: [] },
  { sha: '72d5e0b', msg: 'Fix history filter ignoring e-mail addresses', author: 'Istvan', date: '2019-09-12 18:33', refs: [] },
  { sha: 'b6a0f14', msg: 'Fix diff colours in the light theme', author: 'Andras', date: '2019-02-26 14:47', refs: [] },
  { sha: '0d9e3c7', msg: 'Fix startup outside a repository', author: 'Halina', date: '2018-08-08 08:08', refs: [] },
  { sha: 'e5b27f6', msg: 'Fix commit dialog losing the amend state', author: 'Istvan', date: '2018-01-30 12:19', refs: [] },
  { sha: '83f1a0d', msg: 'Fix crash on an empty .gitmodules', author: 'Andras', date: '2017-07-05 17:36', refs: [] },
  { sha: '2c4d9b8', msg: 'Fix scrollbar jumping while matches stream in', author: 'Halina', date: '2017-02-14 09:44', refs: [] },
  { sha: 'a7e03f5', msg: 'Fix pull with a detached HEAD', author: 'Istvan', date: '2016-10-10 10:10', refs: [] },
  { sha: '61b8c2e', msg: 'Fix tag chips for annotated tags', author: 'Andras', date: '2016-03-22 15:55', refs: [] },
  { sha: 'd90f47a', msg: 'Fix line numbers in the unified diff', author: 'Halina', date: '2015-09-01 11:11', refs: [] },
  { sha: '3b6e8d1', msg: 'Fix the first commit of the project', author: 'Istvan', date: '2015-01-19 19:19', refs: [] },
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
  const t = T(), d = o.d || REGULAR, y = BAR; let x = d.margin, h = TOP_BAR;
  const page = o.page || 'changes';
  // The page tabs' form for the room between the groups (lo..hi): labels, then icons, then — stacked,
  // where even the icons would touch a group — a row of their own under the controls.
  const navForm = (lo, hi) => {
    let items = navItems(lv, page, true), w = measureSegmented(items);
    if (w > hi - lo) { items = navItems(lv, page, false); w = measureSegmented(items); }
    return { items, w, own: w > hi - lo && stacked(lv) };
  };
  c.group('TopBar', () => {
    // The repo and branch chips wear their names at every width (the repo chip never shrinks to a bare
    // folder); where the stacked row cannot hold them whole they give way together (fitNames()).
    const [repo, branch] = fitNames(W, lv, d, 'omagit', o.merging ? 'main · merging feature/askpass' : o.branch || 'main');
    const w = button(c, { x, y, variant: 'ghost', icon: 'folderOpen', label: repo, chevron: true, id: 'RepoChip' });
    x += w + GAP.cluster;
    // branch chip (accent, bold); the menus and the New branch card hang from it
    (c.anchors = c.anchors || {}).BranchChip = { x, y };
    const bw = button(c, { x, y, variant: 'ghost', icon: 'branch', label: branch, chevron: true, weight: 700, iconFill: t.accent, id: 'BranchChip' });
    // paint label accent: overlay
    c.add(`<g id="BranchChip/label-accent">`); text(c, x + PAD.control + BOX.icon + GAP.icon, y + BOX.control / 2, branch, { weight: 700, fill: t.accent }); c.add('</g>');
    x += bw;
    if (o.merging) { x += GAP.cluster; rect(c, x, y + (BOX.control - BOX.pill) / 2, BOX.pill, BOX.pill, { fill: t.red, fo: 0.18 }); text(c, x + BOX.pill / 2, y + BOX.control / 2, '!', { size: 10, weight: 700, fill: t.red, anchor: 'middle' }); x += BOX.pill; }
    // right side, right to left; rx ends on the left edge of the right-hand group
    let rx = W - d.margin;
    if (wide(lv) || lv === 'm') {
      // layout toggles
      rx -= BOX.control; button(c, { x: rx, y, w: BOX.control, variant: 'ghost', icon: 'dockRight', state: o.diffHidden ? 'normal' : 'selected', id: 'Toggle/diff pane' });
      rx -= GAP.cluster + BOX.control; button(c, { x: rx, y, w: BOX.control, variant: 'ghost', icon: 'mini', state: o.mini ? 'selected' : 'normal', id: 'Toggle/mini' });
      rx -= GAP.group / 2; vline(c, rx, y + (BOX.control - BOX.divider) / 2, BOX.divider, { fo: 0.2 }); rx -= GAP.group / 2;
      // More: Fetch and Merge fold into it on M; a shallow window keeps it at every width, as the home of
      // the CHANGES and MESSAGE rows' controls (the page folds those rows away)
      if (lv === 'm' || o.shallow) { rx -= BOX.control; button(c, { x: rx, y, w: BOX.control, icon: 'dots', id: 'More' }); rx -= GAP.item; }
      const labels = lv === 'xl';
      const items = lv === 'm' ? [['push', 'Push', 1], ['pull', 'Pull', 2]] : [['merge', 'Merge'], ['fetch', 'Fetch'], ['push', 'Push', 1], ['pull', 'Pull', 2]];
      // item gaps (8) leave room for the badges hanging 4 past each button's right edge
      items.forEach(([ic, lb, b], i) => {
        if (i) rx -= GAP.item;
        const w = measureButton({ icon: ic, label: labels ? lb : '' });
        rx -= w; button(c, { x: rx, y, icon: ic, label: labels ? lb : '', badge: o.noUpstream ? undefined : b, mark: ic === 'merge' && o.merging, id: 'Sync/' + lb });
      });
    } else if (navForm(x + GAP.group, rx - BOX.control - GAP.item - SYNC_W - GAP.group).own) {
      // Two rows: the borderless sync dropdown alone ends the first row, the names giving way only where
      // it would come within a cluster of the branch; More ends the tabs' row below (drawn with them).
      const sx = W - d.margin - SYNC_W;
      c.record({ k: 'SyncDropdown', id: 'SyncDropdown', x: sx, y, w: SYNC_W, h: BOX.control, down: 2, up: 1 });
      syncDropdown(c, sx, y, 2, 1);
    } else {
      rx -= BOX.control; button(c, { x: rx, y, w: BOX.control, icon: 'dots', id: 'More' }); rx -= GAP.item;
      // one borderless sync dropdown carrying both counts
      c.record({ k: 'SyncDropdown', id: 'SyncDropdown', x: rx - SYNC_W, y, w: SYNC_W, h: BOX.control, down: 2, up: 1 });
      syncDropdown(c, rx - SYNC_W, y, 2, 1);
      rx -= SYNC_W;
    }
    // page tabs: a view toggle centred in the window, like a toolbar mode switch.
    // It keeps a group gap clear of the repo/branch group and the sync group: nudged aside first, labels → icons second.
    // Extra narrow (stacked, and even the icons would touch a group, ≈ < 420): it takes a row of its own under
    // the controls, the bar's width but for an item gap and More at its end, its segments stretched and
    // labelled again (icons if a label does not fit a segment), so the branch keeps its name and nothing
    // overlaps.
    {
      const own = stacked(lv) && navForm(x + GAP.group, W - d.margin - BOX.control - GAP.item - SYNC_W - GAP.group).own;
      const lo = x + GAP.group, hi = rx - GAP.group;
      let { items, w } = navForm(lo, hi);
      if (own) {
        const ry = y + BOX.control + BAR, rw = W - 2 * d.margin - GAP.item - BOX.control, each = Math.floor(rw / 3);
        items = navItems(lv, page, true);
        if (items.some(it => measureSegmented([it]) > each)) items = navItems(lv, page, false);
        segmented(c, { x: d.margin, y: ry, w: rw, stretch: true, id: 'NavTabs', items });
        button(c, { x: W - d.margin - BOX.control, y: ry, w: BOX.control, icon: 'dots', id: 'More' });
        h = TOP_BAR + BOX.control + BAR; // 8 + 28 + 8 + 28 + 8 = 80
      } else {
        const sx = Math.max(lo, Math.min(Math.round(W / 2 - w / 2), hi - w));
        segmented(c, { x: sx, y, id: 'NavTabs', items });
      }
    }
    hairline(c, 0, h - 1, W, { fo: 0.12 });
  });
  return h;
}

// The stacked top bar's names: whole wherever the first row holds them next to the sync group. Where it
// cannot (two rows, long names) they give way together, as the app's TopBar::shareRoom() shares the room:
// the shorter one stays whole while the longer keeps at least as much of itself, and each keeps one
// character before its "…". The font is monospaced, so characters stand for pixels.
function fitNames(W, lv, d, repo, branch) {
  if (!stacked(lv)) return [repo, branch];
  const cw = tw('x'.repeat(10)) / 10, chrome = measureButton({ icon: 'folderOpen', label: '', chevron: true }) + GAP.icon;
  // the two-row first row: repo and branch a cluster apart, the sync dropdown at least a cluster after
  const room = W - 2 * d.margin - 2 * chrome - 2 * GAP.cluster - SYNC_W;
  const chars = Math.floor(room / cw), a = repo.length, b = branch.length;
  if (a + b <= chars) return [repo, branch];
  let cap = 1;
  while (Math.min(a, cap + 1) + Math.min(b, cap + 1) <= chars) cap++;
  let ra = Math.min(a, cap), rb = Math.min(b, cap);
  if (ra + rb < chars) { if (ra < a) ra++; else if (rb < b) rb++; }
  const cut = (s, n) => n >= s.length ? s : s.slice(0, Math.max(0, n - 1)) + '…';
  return [cut(repo, ra), cut(branch, rb)];
}

// The stacked top bar's sync dropdown, borderless until hovered and without a chevron, on one row and on
// two: [4][↓ 16][2][8][↑ 16][1][6], a digit in an 8 slot, 66 wide. The 6 after the last slot leaves as much
// bare room after the count's ink as before the down arrow's (≈ 7.4 either side at 12 px), so the hover
// frame sits evenly around what it holds (the app measures both sides by ink).
const SYNC_W = 66;
function syncDropdown(c, x, y, down, up) {
  const t = T(), mid = y + BOX.control / 2, iy = y + (BOX.control - BOX.icon) / 2;
  c.group('SyncDropdown', () => { // replace-instances.js finds the placement by this name
    icon(c, 'down', x + 4, iy); text(c, x + 20, mid, String(down), { weight: 700, fill: t.accent });
    icon(c, 'up', x + 36, iy); text(c, x + 52, mid, String(up), { weight: 700, fill: t.accent });
  });
}

// ------------------------------------------------------------ page tabs
// The page tabs: Changes | History, plus Diff when the layout is stacked.
function navItems(lv, page, labels) {
  const items = [{ icon: 'commit', label: labels ? 'Changes' : '', count: 7, selected: page === 'changes' }];
  if (stacked(lv)) items.push({ icon: 'diff', label: labels ? 'Diff' : '', selected: page === 'diff' });
  items.push({ icon: 'history', label: labels ? 'History' : '', selected: page === 'history' });
  return items;
}

// ------------------------------------------------------------ changes page
// Icon buttons in a header row stand 4 in from the column's edge, so their
// glyph sits on the 8 px text padding of the boxes below (the St pills, the
// sparkle in the message box).
const headerButtonX = (x, w) => x + w - GAP.icon - BOX.row;
function changesPage(c, x, y, w, h, lv, o = {}) {
  const t = T(), d = o.d || REGULAR; let cy = y;
  const shallow = o.shallow;
  c.group('CommitPage', () => {
    // Section header rows are 24 px (label and 24 px icon buttons centred),
    // their content 8 below, sections a block gap apart like every other
    // part of the pane. CHANGES comes first and MESSAGE last, so the message
    // is written right above Commit.
    const HR = BOX.row, HGAP = GAP.header, SGAP = d.block;
    // the message box: 5, 4, 3 lines of 16 inside 8 px padding; one line (a field) when shallow
    const mh = shallow ? BOX.control : lv === 'xl' ? 96 : lv === 'l' ? 80 : 64;
    // Folded (a two-row bar or a shallow window): no header rows. The list starts at the pane's top and
    // the box stands a block gap under it; the rows' controls are the top bar's More menu's first entries
    // (Files view ›, Show unversioned files, Agent settings…), Refresh is More's own.
    if (o.folded) {
      const tableH = h - SGAP - mh - d.block - BOX.control;
      changesTable(c, x, cy, w, tableH, lv, o);
      cy += tableH + SGAP;
      messageBox(c, x, cy, w, mh, o, shallow);
      cy += mh + d.block;
      actionBar(c, x, cy, w, lv, o);
      return;
    }
    // CHANGES header: the title carries the checked / total count (it used to be
    // a separate "n / m selected" label, which crowded the row on narrow panes)
    const files = o.rows || FILES, checked = files.filter(f => f.checked).length;
    sectionLabel(c, x, cy + HR / 2, 'Changes · ' + checked + '/' + files.length, { id: 'Section/Changes' });
    // right to left: Refresh (it reloads this list) | the unversioned-files filter | how the files are
    // listed (tree, the default at every width, then compact, table); dividers stand in 16 px group gaps
    let ex = headerButtonX(x, w);
    button(c, { x: ex, y: cy, w: HR, h: HR, variant: 'ghost', icon: 'refresh', id: 'Refresh' });
    ex -= GAP.group / 2; vline(c, ex, cy + (HR - BOX.divider) / 2, BOX.divider, { fo: 0.25 }); ex -= GAP.group / 2 + HR;
    button(c, { x: ex, y: cy, w: HR, h: HR, variant: 'ghost', icon: 'eye', state: o.unversioned === false ? 'normal' : 'selected', id: 'ShowUnversioned' });
    ex -= GAP.group / 2; vline(c, ex, cy + (HR - BOX.divider) / 2, BOX.divider, { fo: 0.25 }); ex -= GAP.group / 2;
    const view = filesView(lv, o), vx = ex - 3 * HR - 2 * GAP.cluster;
    FILE_VIEWS.forEach(([id, ic], i) => button(c, { x: vx + i * (HR + GAP.cluster), y: cy, w: HR, h: HR, variant: 'ghost', icon: ic, state: view === id ? 'selected' : 'normal', id: 'FilesView/' + id }));
    cy += HR + HGAP;
    // table: whatever the other parts leave
    const tableH = h - (cy - y) - SGAP - HR - HGAP - mh - d.block - BOX.control;
    changesTable(c, x, cy, w, tableH, lv, o);
    cy += tableH + SGAP;
    // MESSAGE
    sectionLabel(c, x, cy + HR / 2, 'Message');
    button(c, { x: headerButtonX(x, w), y: cy, w: HR, h: HR, variant: 'ghost', icon: 'cog', id: 'AgentSettings' });
    cy += HR + HGAP;
    messageBox(c, x, cy, w, mh, o, shallow);
    cy += mh + d.block;
    // action row
    actionBar(c, x, cy, w, lv, o);
  });
}

// The message box: text 8 in, lines 16 apart. The sparkle's glyph sits on the
// text padding (its 24 button 4 in); in the one-line form it is centred.
function messageBox(c, x, y, w, h, o, oneLine) {
  const t = T(), P = PAD.control;
  c.group('MessageBox', () => {
    fillBox(c, x, y, w, h, 0.04); border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    const first = oneLine ? y + h / 2 : y + P + BOX.line / 2;
    if (o.message) {
      text(c, x + P, first, o.message, { fill: t.fg });
      if (o.messageBody && !oneLine) o.messageBody.forEach((l, i) => text(c, x + P, first + BOX.line * (i + 1), l, { fill: t.fg }));
    } else text(c, x + P, first, 'Commit message', { fill: t.dim });
    const inset = oneLine ? (h - BOX.row) / 2 : P - GAP.icon;
    button(c, { x: x + w - inset - BOX.row, y: y + inset, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'sparkle', id: 'Generate' });
  });
}

const FILE_VIEWS = [['tree', 'tree'], ['compact', 'list'], ['table', 'table']];
const FILE_VIEW_NAMES = { compact: 'Compact list', tree: 'Tree', table: 'Table' };
const filesView = (lv, o) => o.filesView || 'tree';
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
// Rows and the header are 24; the header's hairline is its last pixel row. The
// check and St columns are 32: a 16 box with 8 either side. Cells pad text by 8.
// Tree levels nest by 16, so a file's name starts under its folder's icon.
const INDENT = 16;
function changesTable(c, x, y, w, h, lv, o = {}) {
  const t = T(), rh = BOX.row, hh = BOX.row, P = PAD.control, view = filesView(lv, o);
  c.group(view === 'tree' ? 'ChangesTree' : 'ChangesTable', () => {
    border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    // columns
    const wide = lv === 'xl' || lv === 'l';
    const cols = view === 'compact' ? [['', 32], ['Name', 0], ['St', 32]]
      : view === 'tree' ? (wide ? [['', 32], ['Name', 0], ['+ −', 72], ['St', 32]] : [['', 32], ['Name', 0], ['St', 32]])
      : lv === 'xl' ? [['', 32], ['Name', 0], ['Path', 128], ['Status', 72], ['+ −', 72], ['Size', 72]]
      : lv === 'l' ? [['', 32], ['Name', 0], ['Path', 120], ['Status', 72], ['+ −', 60]]
      : lv === 'm' ? [['', 32], ['Name', 0], ['Path', 112], ['St', 32]]
      : [['', 32], ['Name', 0], ['St', 32]];
    const fixed = cols.reduce((a, [, cw]) => a + cw, 0); cols.find(cc => cc[1] === 0)[1] = w - fixed;
    const box = (cw, s) => (cw - s) / 2;
    // header
    let cx = x; hairline(c, x, y + hh - 1, w, { fo: 0.2 });
    cols.forEach(([name, cw], i) => {
      if (i > 0) vline(c, cx, y + 1, hh - 2, { fo: 0.1 });
      if (name === '') { const rows = o.rows || FILES; checkbox(c, { x: cx + box(cw, BOX.check), y: y + (hh - BOX.check) / 2, checked: rows.every(f => f.checked) ? true : rows.some(f => f.checked) ? 'partial' : false, id: 'CheckAll' }); }
      if (name) text(c, name === 'Name' || name === 'Path' ? cx + P : cx + cw / 2, y + hh / 2, name, { size: 10, weight: 700, fill: t.dim, anchor: name === 'Name' || name === 'Path' ? 'start' : 'middle' });
      cx += cw;
    });
    // rows
    let ry = y + hh;
    const rows = o.rows || FILES;
    const lines = view === 'tree' ? fileTree(rows, o.collapsed || ['tests']) : rows.map(f => ({ kind: 'file', depth: 0, f }));
    lines.forEach((ln) => {
      if (ry + rh > y + h) return;
      const f = ln.f || { name: ln.name, checked: ln.checked, st: '' };
      const indent = ln.depth * INDENT;
      c.add(`<g id="${K.esc((ln.kind === 'dir' ? 'DirRow/' + (ln.open ? 'open' : 'collapsed') : 'Row/' + (f.selected ? 'selected' : 'normal')) + ' ' + f.name)}">`);
      if (f.selected) fillBox(c, x + 1, ry, w - 2, rh, 0.08);
      let cx = x;
      cols.forEach(([name, cw]) => {
        const mid = ry + rh / 2;
        if (name === '') checkbox(c, { x: cx + box(cw, BOX.check), y: ry + (rh - BOX.check) / 2, checked: f.checked });
        else if (name === 'Name' && ln.kind === 'dir') {
          // [8][chevron 12][4][folder 16][4][name]
          const ix = cx + P + indent;
          icon(c, ln.open ? 'chevron' : 'chevronR', ix, ry + (rh - BOX.chevron) / 2, BOX.chevron, { fill: t.dim });
          icon(c, 'folderOutline', ix + BOX.chevron + GAP.icon, ry + (rh - BOX.icon) / 2, BOX.icon, { fill: t.dim });
          const nx = ix + BOX.chevron + GAP.icon + BOX.icon + GAP.icon;
          text(c, nx, mid, ln.name, { fill: t.fg });
          if (!ln.open) text(c, nx + tw(ln.name) + GAP.item, mid, ln.files.length + (ln.files.length === 1 ? ' file' : ' files'), { fill: t.dim, size: SIZE.small });
        }
        else if (name === 'Name') {
          const col = f.selected ? t.accent : (f.st === '?' ? t.dim : statusColor(f.st));
          const nx = cx + P + (view === 'tree' ? indent : 0);
          text(c, nx, mid, f.name, { fill: col, weight: f.st === 'C' ? 700 : 400 });
          if (view === 'compact' && f.path) text(c, nx + tw(f.name) + GAP.item, mid, f.path + '/', { fill: t.dim, size: SIZE.small });
        }
        else if (ln.kind === 'dir') { /* directories carry no status cells */ }
        else if (name === 'Path') text(c, cx + P, mid, f.path, { fill: t.dim });
        else if (name === 'Status') text(c, cx + cw / 2, mid, K.statusName(f.st), { fill: statusColor(f.st), anchor: 'middle', size: SIZE.small });
        else if (name === 'St') statusPill(c, cx + box(cw, BOX.pill), ry + (rh - BOX.pill) / 2, f.st);
        else if (name === '+ −') { if (f.add || f.del) { text(c, cx + cw / 2 - 2, mid, '+' + f.add, { fill: t.green, anchor: 'end', size: SIZE.small }); text(c, cx + cw / 2 + 2, mid, '−' + f.del, { fill: t.red, size: SIZE.small }); } }
        else if (name === 'Size') text(c, cx + cw - P, mid, f.size, { fill: t.dim, anchor: 'end', size: SIZE.small });
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

// Bottom action row of the commit page (y: its top): options left, Commit right; always the same place.
function actionBar(c, x, y, w, lv, o = {}) {
  const t = T(), H = BOX.control;
  c.group('ActionBar', () => {
    const label = o.amend ? 'Amend' : o.merging ? 'Commit merge' : commitLabel(o.rows || FILES);
    if (stacked(lv) || o.shallow) {
      let bx = x;
      bx += button(c, { x: bx, y, w: H, icon: 'dots', id: 'Options' }) + GAP.item;
      if (o.merging) { bx += button(c, { x: bx, y, variant: 'danger', label: 'Abort', id: 'AbortMerge' }) + GAP.item; }
      button(c, { x: bx, y, w: x + w - bx, variant: 'primary', icon: 'commit', label: label, px: PAD.primary, state: o.commitDisabled ? 'disabled' : 'normal', id: 'Commit' });
    } else {
      let bx = x;
      // amend sits by the Commit it changes (the button reads "Amend" once it is on)
      bx += checkbox(c, { x: bx, y: y + (H - BOX.check) / 2, checked: !!o.amend, label: lv === 'm' ? 'Amend' : 'Amend last commit', id: 'AmendLastCommit' }) + GAP.group;
      if (o.merging) { bx += button(c, { x: bx, y, variant: 'danger', icon: 'close', label: 'Abort merge', id: 'AbortMerge' }) + GAP.item; }
      dimText(c, bx, y + H / 2, o.hint || '', { size: SIZE.small });
      const full = label + '  ⏎', cw = measureButton({ icon: 'commit', label: full, px: PAD.primary });
      button(c, { x: x + w - cw, y, w: cw, variant: 'primary', icon: 'commit', label: full, px: PAD.primary, state: o.commitDisabled ? 'disabled' : 'normal', id: 'Commit' });
    }
  });
}

// ------------------------------------------------------------ history page
// The filter row is a control row (28 + 4, the same band as the diff
// toolbar's), then the commits, the details card and the commit's files a block
// gap apart, and the count row (24) at the bottom.
function historyPage(c, x, y, w, h, lv, o = {}) {
  const t = T(), d = o.d || REGULAR; let cy = y;
  const s = o.search; // a filter's matches, see searchCountRow()
  c.group('HistoryPage', () => {
    // filter row: the field takes what All branches and Refresh leave (and,
    // proposal B, the no-limit toggle between them)
    const abw = stacked(lv) ? BOX.control : measureButton({ icon: 'branch', label: 'All branches' });
    const rowToggle = s && s.proposal === 'B';
    const fw = w - GAP.item - abw - GAP.item - BOX.control - (rowToggle ? BOX.control + GAP.item : 0);
    const ph = fw < 300 ? (fw < 200 ? 'Filter' : 'Filter commits') : 'Filter by message, author or SHA';
    field(c, { x, y: cy, w: fw, icon: 'search', placeholder: ph, value: s ? s.value : '', trailingIcon: s ? 'close' : '' });
    if (s && (s.proposal === 'C' || s.proposal === 'D')) {
      // proposals C and D: a 24 px ghost button inside the field, 4 before its clear glyph
      button(c, { x: x + fw - PAD.control - BOX.icon - GAP.icon - BOX.row, y: cy + (BOX.control - BOX.row) / 2, w: BOX.row, h: BOX.row, variant: 'ghost',
        icon: s.proposal === 'C' ? 'infinity' : 'tune', state: s.on || s.menu ? 'selected' : 'normal', id: s.proposal === 'C' ? 'NoLimit' : 'SearchOptions' });
    }
    button(c, { x: x + fw + GAP.item, y: cy, w: abw, icon: 'branch', label: stacked(lv) ? '' : 'All branches', state: o.allBranches ? 'selected' : 'normal', id: 'AllBranches' });
    if (rowToggle) button(c, { x: x + fw + GAP.item + abw + GAP.item, y: cy, w: BOX.control, icon: 'infinity', state: s.on ? 'selected' : 'normal', id: 'NoLimit' });
    button(c, { x: x + w - BOX.control, y: cy, w: BOX.control, icon: 'refresh', id: 'Refresh' });
    cy += BOX.control + GAP.controlRow;
    const detailsH = o.shallow ? 0 : stacked(lv) ? 132 : 152;
    const filesH = o.shallow || stacked(lv) ? 0 : 4 * BOX.row;
    const tableH = h - (cy - y) - (detailsH ? d.block + detailsH : 0) - (filesH ? d.block + filesH : 0) - BOX.row;
    commitsTable(c, x, cy, w, tableH, lv, o);
    cy += tableH;
    if (detailsH) { cy += d.block; commitDetails(c, x, cy, w, detailsH, lv, o); cy += detailsH; }
    if (filesH) { cy += d.block; changesTable(c, x, cy, w, filesH, lv, { filesView: 'table', rows: [{ name: 'Toolbar.cpp', path: 'src/ui', ext: '.cpp', size: '6.2 KiB', st: 'M', add: 4, del: 2, checked: true, selected: true }, { name: 'Toolbar.h', path: 'src/ui', ext: '.h', size: '1.9 KiB', st: 'M', add: 3, del: 0, checked: true }].map(r => ({ ...r, checked: undefined })) }); cy += filesH; }
    if (s) searchCountRow(c, x, cy, w, s);
    else dimText(c, x, cy + BOX.row / 2, '12 commits', { size: SIZE.small });
  });
}

// The count row of a filter that stopped at the 10,000-match cap (`on`: the
// full search running). Proposal A carries its own action on the row; the
// others point at their control while capped.
function searchCountRow(c, x, y, w, s) {
  const mid = y + BOX.row / 2;
  const count = s.on ? 'Searching… 14,203 matches' : s.proposal === 'A' || s.proposal === 'D' ? '10,000+ matches' : '10,000+ matches — ∞ finds all';
  dimText(c, x, mid, count, { size: SIZE.small, id: 'SearchCount' });
  if (s.proposal !== 'A') return;
  const action = s.on ? { icon: 'stop', label: 'Stop', id: 'StopSearch' } : { icon: 'infinity', label: 'Find all matches', id: 'FindAll' };
  const bw = measureButton(action);
  button(c, { x: x + w - bw, y, h: BOX.row, variant: 'ghost', ...action });
}

function commitsTable(c, x, y, w, h, lv, o = {}) {
  const t = T(), rh = BOX.row, hh = BOX.row, P = PAD.control;
  c.group('CommitsTable', () => {
    border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    // A filter's matches have no graph (a zero-wide column keeps the indices)
    // and, as in the app since 2026-09-25 (the user's rule), Author in every class.
    const cols = o.search ? (lv === 'xl' ? [['Graph', 0], ['Message', -1], ['Author', 88], ['Date', 128]]
      : lv === 'l' ? [['Graph', 0], ['Message', -1], ['Author', 88], ['Date', 120]]
      : lv === 'm' ? [['Graph', 0], ['Message', -1], ['Author', 88], ['Date', 88]]
      : [['Graph', 0], ['Message', -1], ['Author', 72]])
      : lv === 'xl' ? [['Graph', 40], ['Message', 0], ['Author', 88], ['Date', 128]]
      : lv === 'l' ? [['Graph', 40], ['Message', 0], ['Author', 88], ['Date', 120]]
      : lv === 'm' ? [['Graph', 36], ['Message', 0], ['Date', 88]]
      : [['Graph', 32], ['Message', 0]];
    const stretch = o.search ? -1 : 0;
    const fixed = cols.reduce((a, [, cw]) => a + Math.max(0, cw), 0); cols.find(cc => cc[1] === stretch)[1] = w - fixed;
    let cx = x; hairline(c, x, y + hh - 1, w, { fo: 0.2 });
    cols.forEach(([name, cw], i) => {
      if (i > 0 && cx > x) vline(c, cx, y + 1, hh - 2, { fo: 0.1 });
      if (name !== 'Graph') text(c, cx + P, y + hh / 2, name, { size: 10, weight: 700, fill: t.dim });
      cx += cw;
    });
    let ry = y + hh;
    const gx = x + cols[0][1] / 2;
    (o.search ? FIX_MATCHES : COMMITS).forEach((cm, i) => {
      if (ry + rh > y + h) return;
      const sel = i === (o.commit || 0);
      c.add(`<g id="${K.esc('CommitRow/' + (sel ? 'selected' : 'normal') + ' ' + cm.sha)}">`);
      if (sel) { fillBox(c, x + 1, ry, w - 2, rh, 0.08); (c.anchors = c.anchors || {}).CommitRow = { x, y: ry, w, h: rh, msgX: x + cols[0][1] }; }
      // graph
      if (!o.search) {
        const lx = gx - 6 + cm.lane * 12;
        rect(c, gx - 6 - 0.5, ry, 2, rh, { fill: t.accent, fo: 0.9 });
        if (cm.lane === 1 || (COMMITS[i + 1] && COMMITS[i + 1].lane === 1) || (COMMITS[i - 1] && COMMITS[i - 1].lane === 1)) rect(c, gx + 6 - 0.5, ry, 2, rh, { fill: t.magenta, fo: 0.9 });
        circle(c, lx, ry + rh / 2, 4, { fill: cm.lane ? t.magenta : t.accent, stroke: t.bg, sw: 1.5 });
      }
      let mx = x + cols[0][1] + P;
      cm.refs.forEach(([lb, kind]) => { if (lv === 'xs' && kind === 'remote') return; mx += refChip(c, mx, ry + (rh - BOX.pill) / 2, lb, kind) + GAP.cluster; });
      const avail = cols[1][1] - (mx - (x + cols[0][1])) - P;
      let msg = cm.msg; const maxc = Math.floor(avail / (0.6 * 12)); if (msg.length > maxc) msg = msg.slice(0, Math.max(0, maxc - 1)) + '…';
      text(c, mx, ry + rh / 2, msg, { fill: sel ? t.accent : t.fg });
      let ccx = x + cols[0][1] + cols[1][1];
      cols.slice(2).forEach(([name, cw]) => {
        const v = name === 'Author' ? cm.author : name === 'Date' ? (lv === 'm' ? cm.date.slice(0, 10) : cm.date) : cm.sha;
        text(c, ccx + P, ry + rh / 2, v, { fill: t.dim, size: SIZE.small });
        ccx += cw;
      });
      c.add('</g>');
      ry += rh;
    });
    scrollbar(c, x + w - 9, y + hh + 2, h - hh - 4, { ratio: 0.3 });
  });
}

// A card: 12 padding, the title on a 24 row with the copy button (its glyph on
// the padding), then 16 px text lines.
function commitDetails(c, x, y, w, h, lv, o = {}) {
  const t = T(), P = PAD.popover, L = BOX.line;
  c.group('CommitDetails', () => {
    border(c, x, y, w, h, { stroke: t.fg, so: 0.4 });
    const ci = o.search ? 0 : o.commit || 0, cm = o.search ? FIX_MATCHES[0] : COMMITS[ci], top = y + P - GAP.icon;
    button(c, { x: x + w - (P - GAP.icon) - BOX.row, y: top, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'copy', id: 'CopySha' });
    let cy = top + BOX.row / 2;
    text(c, x + P, cy, cm.msg, { weight: 700, size: SIZE.subtitle });
    cy += BOX.row / 2 + L / 2;
    let mx = x + P;
    mx += text(c, mx, cy, cm.sha, { fill: t.accent, size: SIZE.small }) + P;
    mx += text(c, mx, cy, cm.author + ' <andras@example.org>', { fill: t.dim, size: SIZE.small }) + P;
    if (!stacked(lv)) mx += text(c, mx, cy, cm.date, { fill: t.dim, size: SIZE.small }) + P;
    cy += L + GAP.cluster;
    const parent = 'Parent ' + (o.search ? '6bd79ab' : COMMITS[ci + 1].sha);
    mx = x + P; dimText(c, mx, cy, parent, { size: SIZE.small }); mx += tw(parent, SIZE.small) + P;
    cm.refs.forEach(([lb, kind]) => { mx += refChip(c, mx, cy - BOX.pill / 2, lb, kind) + GAP.cluster; });
    cy += L + GAP.item;
    const body = cm.body || 'The toolbar folds its buttons in three steps now, and the whole window picks a layout from its width and height.';
    const maxc = Math.floor((w - 2 * P) / 7.2); const words = body.split(' '); let line = ''; const lines = [];
    words.forEach(wd => { if ((line + ' ' + wd).trim().length > maxc) { lines.push(line); line = wd; } else line = (line ? line + ' ' : '') + wd; }); lines.push(line);
    const bw = measureButton({ label: '2 files ›' }), limit = y + h - P - (stacked(lv) ? BOX.row : 0);
    lines.forEach(l => { if (cy + L / 2 <= limit) { text(c, x + P, cy, l, { size: SIZE.body }); cy += L; } });
    if (stacked(lv)) button(c, { x: x + w - (P - GAP.icon) - bw, y: y + h - (P - GAP.icon) - BOX.row, h: BOX.row, label: '2 files ›', variant: 'ghost', id: 'ShowFiles' });
  });
}

// ------------------------------------------------------------ diff pane
// The toolbar is a control row (28 + 4). The diff: a 24 header per side, then
// 16 px code lines 4 below it: [mark 20: ± in a 16 box][numbers, right-aligned 8 short of 52][8][code].
function diffPane(c, x, y, w, h, lv, o = {}) {
  const t = T(), H = BOX.control, P = PAD.control;
  const split = o.split ?? (w >= 720);
  c.group('DiffPane', () => {
    // toolbar row
    let tx = x; const ty = y, mid = y + H / 2;
    const compact = w < 560;
    tx += button(c, { x: tx, y: ty, w: compact ? H : undefined, icon: 'up', label: compact ? '' : 'Prev', state: 'disabled', id: 'PrevChange' }) + GAP.cluster;
    tx += button(c, { x: tx, y: ty, w: compact ? H : undefined, icon: 'down', label: compact ? '' : 'Next', id: 'NextChange' }) + GAP.item;
    const run = (s, o2) => { tx += text(c, tx, mid, s, o2); };
    run(compact ? '1/2' : 'Change 1 of 2', { fill: t.dim, size: SIZE.small });
    if (!compact) {
      tx += GAP.item; run('·', { fill: t.dim }); tx += GAP.item; run('Modified', { fill: t.blue, size: SIZE.small });
      tx += GAP.item; run('+4', { fill: t.green, size: SIZE.small }); tx += GAP.cluster; run('−2', { fill: t.red, size: SIZE.small });
    }
    let rx = x + w;
    if (compact) { rx -= H; button(c, { x: rx, y: ty, w: H, icon: 'dots', id: 'DiffMore' }); }
    else {
      const labels = w >= 900;
      rx -= H; button(c, { x: rx, y: ty, w: H, icon: 'code', state: 'selected', id: 'Syntax' }); rx -= GAP.cluster;
      rx -= H; button(c, { x: rx, y: ty, w: H, icon: 'pilcrow', id: 'Whitespace' }); rx -= GAP.cluster;
      const vw = measureButton({ icon: split ? 'split' : 'unified', label: labels ? (split ? 'Split' : 'Unified') : '', chevron: true });
      rx -= vw; button(c, { x: rx, y: ty, icon: split ? 'split' : 'unified', label: labels ? (split ? 'Split' : 'Unified') : '', chevron: true, id: 'ViewMode' });
    }
    const by = y + H + GAP.controlRow, bh = h - (by - y);
    border(c, x, by, w, bh, { stroke: t.fg, so: 0.4 });
    const hh = BOX.row, rh = BOX.line, mark = 20, gutter = 32;
    const drawSide = (sx, sw, side, title, rows) => {
      // header (inside the border's top pixel; its hairline is its last row)
      fillBox(c, sx, by + 1, sw, hh - 1, 0.04);
      const path = 'src/ui/Toolbar.cpp';
      text(c, sx + P, by + hh / 2, path, { weight: 700 });
      // the side's title (HEAD, Working tree) gives way when it would come within a group gap of the path
      if (P + tw(path) + GAP.group + tw(title, SIZE.small) + P <= sw) text(c, sx + sw - P, by + hh / 2, title, { fill: t.dim, anchor: 'end', size: SIZE.small });
      hairline(c, sx, by + hh - 1, sw, { fo: 0.2 });
      const top = by + hh + GAP.cluster; let ry = top;
      const maxRows = Math.floor((by + bh - 1 - top) / rh);
      rows.slice(0, maxRows).forEach(r => {
        const seg = r[side];
        if (seg.k === 'filler') fillBox(c, sx + 1, ry, sw - 2, rh, 0.05);
        else if (seg.k === 'del') fillBox(c, sx + 1, ry, sw - 2, rh, 0.16, { fill: t.red });
        else if (seg.k === 'add') fillBox(c, sx + 1, ry, sw - 2, rh, 0.16, { fill: t.green });
        if (seg.k === 'del' || seg.k === 'add') icon(c, seg.k === 'del' ? 'minus' : 'plus', sx + (mark - BOX.icon) / 2, ry + (rh - BOX.icon) / 2, BOX.icon, { fill: seg.k === 'del' ? t.red : t.green });
        if (seg.n !== undefined) text(c, sx + mark + gutter - P, ry + rh / 2, String(seg.n), { fill: t.dim, anchor: 'end', size: SIZE.small });
        if (seg.t !== undefined) {
          let cx = sx + mark + gutter + P; const maxc = Math.floor((sw - (mark + gutter + 2 * P)) / 7.2);
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
      marks.forEach(([i, k]) => rect(c, sx + sw - 6, top + i * rh, 3, rh, { fill: k === 'del' ? t.red : t.green }));
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
    scrollbar(c, x + w - 16, by + hh + GAP.cluster, bh - hh - 2 * GAP.cluster, { ratio: 0.6 });
  });
}

// ------------------------------------------------------------ mini rail
// 40 px tiles 4 apart (a cluster); the commit tile at the bottom behind a hairline.
function miniRail(c, x, y, h, o = {}) {
  const t = T(), s = BOX.tile, gap = GAP.cluster; let ty = y;
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
    const cy = y + h - s;
    if (o.commitTile !== false && cy >= ty + 12) {
      const n = FILES.filter(f => f.checked).length;
      hairline(c, x, cy - 8, s, { fo: 0.2 });
      c.group('Tile/commit', () => {
        fillBox(c, x, cy, s, s, 0.08, { fill: t.accent }); border(c, x, cy, s, s, { stroke: t.accent, so: 1 });
        icon(c, 'commit', x + (s - BOX.icon) / 2, cy + (s - BOX.icon) / 2 + 2, BOX.icon, { fill: t.accent });
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
  const t = T(), { x, w } = o, P = PAD.popover, inner = w - 2 * P, mh = 80;
  const h = P + BOX.row + GAP.header + mh + GAP.item + BOX.line + 2 * SEP + BOX.control + P;
  const y = o.bottom !== undefined ? o.bottom - h : o.y;
  c.group(o.id || 'CommitPopover', () => {
    rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    let cy = y + P;
    sectionLabel(c, x + P, cy + BOX.row / 2, 'Message');
    button(c, { x: headerButtonX(x + P, inner), y: cy, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'cog', id: 'AgentSettings' });
    cy += BOX.row + GAP.header;
    messageBox(c, x + P, cy, inner, mh, o, false);
    cy += mh + GAP.item;
    const n = FILES.filter(f => f.checked).length;
    dimText(c, x + P, cy + BOX.line / 2, n + ' / ' + FILES.length + ' files selected · Space on a tile toggles it', { size: SIZE.small, id: 'SelectedCount' });
    cy += BOX.line + SEP;
    hairline(c, x + P, cy, inner, { fo: 0.15 }); cy += SEP;
    checkbox(c, { x: x + P, y: cy + (BOX.control - BOX.check) / 2, checked: !!o.amend, label: 'Amend last commit', id: 'AmendLastCommit' });
    const full = (o.amend ? 'Amend' : commitLabel(FILES)) + '  ⏎', cw = measureButton({ icon: 'commit', label: full, px: PAD.primary });
    button(c, { x: x + w - P - cw, y: cy, w: cw, variant: 'primary', icon: 'commit', label: full, px: PAD.primary, id: 'Commit' });
  });
  return h;
}

// ------------------------------------------------------------ new branch card
// The New branch popover (Ctrl+N, the branch menu's last row, a commit's
// menu in History). It hangs where the branch menu does, under the branch
// chip, since that is where the new branch shows up. 12 padding; captions on
// 16 lines 4 over their control; sections 16 apart; a hairline 12 above the
// Switch to it · Create branch row, as in the commit popover.
// o.name: the typed name; o.taken: it names an existing branch; o.base:
// { kind: branch|commit|tag, name, sha, msg, current }; o.blocked: changed
// files git would not carry to the base; o.pickerOpen: the From menu is open.
const HEAD_BASE = { kind: 'branch', name: 'main', sha: 'd444446', msg: 'Make the toolbar tiling aware', current: true };
function newBranchCard(c, o) {
  const t = T(), { x, y, w } = o, P = PAD.popover, inner = w - 2 * P, L = BOX.line;
  const base = o.base || HEAD_BASE, switchTo = !o.blocked;
  c.add(`<g id="${K.esc(o.id || 'NewBranchCard')}">`);
  const bgAt = c.parts.length;
  let cy = y + P;
  const caption = (label, right) => {
    sectionLabel(c, x + P, cy + L / 2, label);
    if (right) dimText(c, x + w - P, cy + L / 2, right, { anchor: 'end', size: SIZE.caption });
    cy += L + GAP.caption;
  };
  // NAME: focused, the caret after what was typed (or carried over from the menu's search)
  caption('Name');
  field(c, { x: x + P, y: cy, w: inner, state: 'focus', value: o.name || '', placeholder: 'feature/…', id: 'BranchName' });
  cy += BOX.control;
  if (o.taken) { // on a 24 row 4 under the field: why Create is off, and the way out
    cy += GAP.caption;
    icon(c, 'warn', x + P, cy + (BOX.row - BOX.icon) / 2, BOX.icon, { fill: t.red });
    text(c, x + P + BOX.icon + GAP.icon, cy + BOX.row / 2, 'Already a branch', { fill: t.red, size: SIZE.small, id: 'NameTaken' });
    const sw = measureButton({ label: 'Switch to it' });
    button(c, { x: x + w - P - sw, y: cy, h: BOX.row, variant: 'ghost', label: 'Switch to it', id: 'SwitchToExisting' });
    cy += BOX.row;
  }
  cy += GAP.group;
  // FROM: where it starts, a picker like the merge dialog's, the commit it resolves to under it
  caption('From', base.current ? 'the current branch' : base.kind === 'commit' ? 'the commit picked in History' : '');
  const pickIcon = base.kind === 'commit' ? 'commit' : base.kind === 'tag' ? 'tag' : 'branch';
  button(c, { x: x + P, y: cy, w: inner, icon: pickIcon, label: base.name, chevron: true, weight: 700, state: o.pickerOpen ? 'pressed' : 'normal', id: 'BasePicker' });
  (c.anchors = c.anchors || {}).BasePicker = { x: x + P, y: cy, w: inner, h: BOX.control };
  cy += BOX.control + GAP.caption;
  {
    // a branch or tag says which commit it is; a commit (named by its SHA already) gives its subject
    const shaW = base.kind === 'commit' ? 0 : text(c, x + P, cy + L / 2, base.sha, { fill: t.accent, size: SIZE.small, id: 'BaseSha' }) + GAP.item;
    const room = Math.floor((inner - shaW) / (0.6 * SIZE.small)) - 1;
    const msg = base.msg.length > room ? base.msg.slice(0, room - 1) + '…' : base.msg;
    text(c, x + P + shaW, cy + L / 2, msg, { fill: t.dim, size: SIZE.small, id: 'BaseSubject' });
  }
  cy += L + GAP.group;
  // the working tree: git switch -c carries the changes along unless the base
  // has other versions of those files; then it can only be created, not switched to
  const n = FILES.length;
  if (o.blocked) {
    // a note card: 12 padding, the icon's 16 box then 8, a bold line and 16 px lines 4 under it
    const b = o.blocked, bh = P + L + GAP.caption + b.lines.length * L + P, tx = x + P + P + BOX.icon + GAP.item;
    c.group('BlockedNote', () => {
      fillBox(c, x + P, cy, inner, bh, 0.04); border(c, x + P, cy, inner, bh, { stroke: t.yellow, so: 0.6 });
      icon(c, 'alert', x + P + P, cy + P, BOX.icon, { fill: t.yellow });
      text(c, tx, cy + P + L / 2, b.title, { weight: 700, fill: t.yellow });
      b.lines.forEach((l, i) => text(c, tx, cy + P + L + GAP.caption + i * L + L / 2, l, { fill: t.dim, size: SIZE.small }));
    });
    cy += bh;
  } else {
    icon(c, 'info', x + P, cy, BOX.icon, { fill: t.dim });
    text(c, x + P + BOX.icon + GAP.icon, cy + L / 2, 'Your ' + n + ' changed files come along.', { fill: t.dim, size: SIZE.small, id: 'CarryNote' });
    cy += L;
  }
  cy += SEP;
  hairline(c, x + P, cy, inner, { fo: 0.15 }); cy += SEP;
  checkbox(c, { x: x + P, y: cy + (BOX.control - BOX.check) / 2, checked: switchTo, disabled: !switchTo, label: 'Switch to it', id: 'SwitchToIt' });
  const label = 'Create branch  ⏎', bw = measureButton({ icon: 'branchPlus', label, px: PAD.primary });
  button(c, { x: x + w - P - bw, y: cy, w: bw, variant: 'primary', icon: 'branchPlus', label, px: PAD.primary, state: o.taken || !o.name ? 'disabled' : 'normal', id: 'CreateBranch' });
  cy += BOX.control;
  const h = cy + P - y;
  c.insertAt(bgAt, () => { rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 }); });
  c.add('</g>');
  return h;
}

// ------------------------------------------------------------ footer
// 28 high with its hairline as the top pixel row; the status text on the side
// margin, the keys button flush with the other margin and the settings cog an
// item gap before it (2026-09-26, as in the app).
function footer(c, W, H, lv, o = {}) {
  const t = T(), d = o.d || REGULAR, h = BOX.footer, y = H - h, m = d.margin;
  c.group('Footer', () => {
    hairline(c, 0, y, W, { fo: 0.12 });
    const st = o.status || (lv === 'xs' ? '~/Projects/omagit' : 'Fetched origin 2 min ago · ~/Projects/omagit');
    if (o.busy) { icon(c, 'loader', m, y + (h - BOX.icon) / 2, BOX.icon, { fill: t.accent }); text(c, m + BOX.icon + GAP.icon, y + h / 2, st, { fill: t.dim, size: SIZE.small }); }
    else text(c, m, y + h / 2, st, { fill: t.dim, size: SIZE.small });
    const by = y + (h - BOX.row) / 2;
    button(c, { x: W - m - 2 * BOX.row - GAP.item, y: by, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'cog', id: 'Settings' });
    button(c, { x: W - m - BOX.row, y: by, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'keyboard', id: 'Keybindings' });
  });
  return h;
}

// ------------------------------------------------------------ menus / cards
// A menu: 4 around its rows; 24 px rows with [8][icon 16][4][label … hint][8];
// section captions on 24 rows; separators 8 with the hairline in the middle;
// the search prompt 28 over a separator.
const menuRowH = it => it.type === 'search' ? BOX.control + 8 : it.type === 'sep' ? 8 : BOX.row;
const menuHeight = items => 2 * PAD.menu + items.reduce((a, it) => a + menuRowH(it), 0);
function menuCard(c, o) {
  const t = T(), { x, y, w } = o, pad = PAD.menu, P = PAD.control, rh = BOX.row, iy = (rh - BOX.icon) / 2;
  const h = o.h || menuHeight(o.items);
  c.group(o.id || 'MenuCard', () => {
    rect(c, x, y, w, h, { fill: t.bg });
    border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    let cy = y + pad;
    o.items.forEach(it => {
      if (it.type === 'search') { field(c, { x: x + pad, y: cy, w: w - pad * 2, icon: 'search', placeholder: it.label, state: 'prompt', value: it.value }); hairline(c, x + pad + 4, cy + BOX.control + 4, w - pad * 2 - 8); cy += menuRowH(it); return; }
      if (it.type === 'section') { sectionLabel(c, x + pad + P, cy + rh / 2, it.label); cy += rh; return; }
      if (it.type === 'sep') { hairline(c, x + pad + 4, cy + 4, w - pad * 2 - 8); cy += menuRowH(it); return; }
      c.add(`<g id="${K.esc('MenuRow/' + (it.hover ? 'hover' : 'normal') + ' ' + it.label)}">`);
      if (it.hover) fillBox(c, x + pad, cy, w - pad * 2, rh, 0.08);
      let ix = x + pad + P;
      if (it.icon) { icon(c, it.icon, ix, cy + iy, BOX.icon, { fill: it.hover ? t.accent : t.fg, opacity: it.disabled ? 0.45 : 1 }); ix += BOX.icon + GAP.icon; }
      text(c, ix, cy + rh / 2, it.label, { fill: it.hover ? t.accent : t.fg, opacity: it.disabled ? 0.45 : 1 });
      // the hint gives way where it would come within an item gap of the label (a menu clamped to a narrow window)
      const hintRight = x + w - pad - P - (it.checked || it.submenu ? BOX.icon + GAP.icon : 0);
      if (it.hint && hintRight - tw(it.hint, SIZE.small) - (ix + tw(it.label)) >= GAP.item) text(c, hintRight, cy + rh / 2, it.hint, { fill: t.dim, anchor: 'end', size: SIZE.small });
      if (it.checked) icon(c, 'check', x + w - pad - P - BOX.icon, cy + iy, BOX.icon, { fill: t.accent });
      // a submenu's chevron: in the tick's 16 px box, at the chevrons' 12, dim (accent while its row is current)
      if (it.submenu) icon(c, 'chevronR', x + w - pad - P - BOX.icon + (BOX.icon - BOX.chevron) / 2, cy + (rh - BOX.chevron) / 2, BOX.chevron, { fill: it.hover ? t.accent : t.dim });
      c.add('</g>');
      cy += rh;
    });
  });
  return h;
}

// A dialog: 16 padding, the title on a 24 row with the close button 4 in from
// the content edge, the subtitle on the 16 line under it.
function dialogCard(c, o) {
  const t = T(), { x, y, w, h } = o, P = PAD.dialog;
  c.group(o.id || 'Dialog', () => {
    rect(c, x, y, w, h, { fill: t.bg });
    border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    text(c, x + P, y + P + BOX.row / 2, o.title, { size: SIZE.heading, weight: 500 });
    if (o.subtitle) text(c, x + P, y + P + BOX.row + BOX.line / 2, o.subtitle, { fill: t.dim, size: SIZE.small });
    button(c, { x: headerButtonX(x + P, w - 2 * P), y: y + P, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'close', id: 'Close' });
  });
}

// Merge dialog, top to bottom: title and subtitle, the branch pickers (36 px,
// side by side or stacked), the verdict card, the option, Cancel · Merge.
const PICKER = 36, VERDICT = 80;
const DIALOG_HEAD = PAD.dialog + BOX.row + BOX.line + GAP.group;
function mergeDialogHeight(narrow) {
  const pickers = narrow ? BOX.line + GAP.caption + PICKER + GAP.item + BOX.control + GAP.item + PICKER : BOX.line + GAP.caption + PICKER;
  return DIALOG_HEAD + pickers + GAP.group + VERDICT + GAP.group + BOX.check + GAP.group + BOX.control + PAD.dialog;
}
function mergeDialog(c, x, y, w, o = {}) {
  const h = o.h || mergeDialogHeight(w < 520);
  c.group('MergeDialog', () => mergeDialogContent(c, x, y, w, h));
  return h;
}
function mergeDialogContent(c, x, y, w, h) {
  const t = T(), narrow = w < 520, P = PAD.dialog;
  dialogCard(c, { x, y, w, h, title: 'Merge', subtitle: 'Worked out on the trees alone — nothing touches the working tree yet.', id: 'MergeDialog/card' });
  let cy = y + DIALOG_HEAD;
  const inner = w - P * 2, picker = (px, py, pw, label, id) => button(c, { x: px, y: py, w: pw, h: PICKER, px: PAD.big, icon: 'branch', label, chevron: true, weight: 700, id });
  if (narrow) {
    sectionLabel(c, x + P, cy + BOX.line / 2, 'Merge'); cy += BOX.line + GAP.caption;
    picker(x + P, cy, inner, 'feature/askpass', 'BranchPicker/source'); cy += PICKER + GAP.item;
    button(c, { x: x + P, y: cy, w: BOX.control, h: BOX.control, variant: 'ghost', icon: 'swap', id: 'Swap' });
    text(c, x + P + BOX.control + GAP.item, cy + BOX.control / 2, 'INTO', { size: 10, weight: 700, fill: t.dim, ls: 0.8 }); cy += BOX.control + GAP.item;
    picker(x + P, cy, inner, 'main', 'BranchPicker/target'); cy += PICKER + GAP.group;
  } else {
    const pw = Math.floor((inner - PICKER - 2 * GAP.item) / 2), tx = x + P + pw + GAP.item + PICKER + GAP.item;
    sectionLabel(c, x + P, cy + BOX.line / 2, 'Merge'); sectionLabel(c, tx, cy + BOX.line / 2, 'Into'); cy += BOX.line + GAP.caption;
    picker(x + P, cy, pw, 'feature/askpass', 'BranchPicker/source');
    button(c, { x: x + P + pw + GAP.item, y: cy, w: PICKER, h: PICKER, icon: 'swap', id: 'Swap' });
    picker(tx, cy, x + P + inner - tx, 'main', 'BranchPicker/target');
    cy += PICKER + GAP.group;
  }
  // verdict card: 12 padding, the icon's 16 box then 8, a title line and two 16 lines 8 under it
  const cp = PAD.popover, tx = x + P + cp + BOX.icon + GAP.item;
  c.group('VerdictCard', () => {
    fillBox(c, x + P, cy, inner, VERDICT, 0.04); border(c, x + P, cy, inner, VERDICT, { stroke: t.fg, so: 0.4 });
    icon(c, 'checkCircle', x + P + cp, cy + cp, BOX.icon, { fill: t.green });
    text(c, tx, cy + cp + BOX.line / 2, 'Clean merge — no conflicts.', { weight: 700, fill: t.green });
    text(c, tx, cy + cp + BOX.line + GAP.item + BOX.line / 2, narrow ? '2 commits · 3 files' : 'Creates a merge commit bringing 2 commits and 3 changed files into main.', { fill: t.dim, size: SIZE.small });
    text(c, tx, cy + cp + 2 * BOX.line + GAP.item + BOX.line / 2, '+41 −7 lines', { fill: t.dim, size: SIZE.small });
  });
  cy += VERDICT + GAP.group;
  checkbox(c, { x: x + P, y: cy, checked: false, label: 'Always create a merge commit' });
  const by = y + h - P - BOX.control;
  const mw = measureButton({ icon: 'merge', label: 'Merge', px: PAD.primary });
  button(c, { x: x + w - P - mw, y: by, icon: 'merge', label: 'Merge', variant: 'primary', px: PAD.primary, id: 'MergeButton' });
  button(c, { x: x + w - P - mw - GAP.item - measureButton({ label: 'Cancel' }), y: by, label: 'Cancel', id: 'Cancel' });
}

// Settings (2026-09-26, after the app's SettingsDialog): the dialog card's head,
// then a group per subject, a caption over its controls. FILE MANAGER: the
// Nautilus checkbox, and under its text (16 + 8 in) the note on the 16 lines it
// wraps to and, when a running Nautilus has to restart to show the change,
// Restart Nautilus 8 under the note; a group gap, then Close at the right.
const SETTINGS_W = 480;
function wrapLines(str, width, size) {
  const lines = [];
  for (const word of str.split(' ')) {
    const last = lines.length ? lines[lines.length - 1] : null;
    if (last !== null && tw(last + ' ' + word, size) <= width) lines[lines.length - 1] = last + ' ' + word;
    else lines.push(word);
  }
  return lines;
}
function settingsNote(o) {
  return o.restart ? 'Nautilus picks it up once it restarts.' : 'Right-click a folder or file inside a repository in Nautilus to open it here.';
}
function settingsDialogHeight(w, o = {}) {
  const P = PAD.dialog, indent = BOX.check + GAP.check;
  const lines = wrapLines(settingsNote(o), w - 2 * P - indent, SIZE.small).length;
  return DIALOG_HEAD + BOX.line + GAP.caption + BOX.line + GAP.caption + lines * BOX.line
    + (o.restart ? GAP.item + BOX.control : 0) + GAP.group + BOX.control + P;
}
function settingsDialog(c, x, y, w, o = {}) {
  const h = settingsDialogHeight(w, o);
  c.group('SettingsDialog', () => {
    const t = T(), P = PAD.dialog, indent = BOX.check + GAP.check;
    dialogCard(c, { x, y, w, h, title: 'Settings', subtitle: 'Every change applies at once.', id: 'SettingsDialog/card' });
    let cy = y + DIALOG_HEAD;
    sectionLabel(c, x + P, cy + BOX.line / 2, 'File manager'); cy += BOX.line + GAP.caption;
    checkbox(c, { x: x + P, y: cy, checked: !!o.checked, label: 'Show “Open in Omagit” in Nautilus', id: 'NautilusMenu' }); cy += BOX.line + GAP.caption;
    wrapLines(settingsNote(o), w - 2 * P - indent, SIZE.small).forEach((l, i) =>
      text(c, x + P + indent, cy + i * BOX.line + BOX.line / 2, l, { fill: t.dim, size: SIZE.small, id: i ? undefined : 'NautilusNote' }));
    cy += wrapLines(settingsNote(o), w - 2 * P - indent, SIZE.small).length * BOX.line;
    if (o.restart) {
      cy += GAP.item;
      button(c, { x: x + P + indent, y: cy, label: 'Restart Nautilus', id: 'RestartNautilus' });
    }
    const label = 'Close', bw = measureButton({ label, px: PAD.primary });
    button(c, { x: x + w - P - bw, y: y + h - P - BOX.control, label, variant: 'primary', px: PAD.primary, id: 'CloseButton' });
  });
  return h;
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
// The popover's height, from a dry run of its layout.
function agentPopoverHeight(agent = 'claude') { return agentPopover(new K.Canvas('measure', 1, 1), { x: 0, y: 0, w: 360, agent }); }
// 12 padding; captions on 16 lines 4 over their content, sections 16 apart;
// model rows 24; the level track 40; a hairline 12 above Generate now.
function agentPopover(c, o) {
  const t = T(), { x, y, w } = o, P = PAD.popover, inner = w - 2 * P, rh = BOX.row, L = BOX.line;
  const agent = o.agent || 'claude', A = AGENTS[agent], model = o.model ?? 'opus', effort = o.effort ?? 'high';
  const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);
  const levels = A ? ['', ...A.levels] : [];
  c.add(`<g id="${K.esc(o.id || 'AgentPopover')}">`);
  const bgAt = c.parts.length;
  let cy = y + P;
  const caption = (label, right) => {
    sectionLabel(c, x + P, cy + L / 2, label);
    if (right) dimText(c, x + w - P, cy + L / 2, right, { anchor: 'end', size: SIZE.caption });
    cy += L + GAP.caption;
  };
  if (!A) { // nothing installed: say what is missing and how to get it, copyable
    icon(c, 'robot', x + P, cy + GAP.caption, 24, { fill: t.dim });
    text(c, x + P + 24 + GAP.item, cy + L / 2, 'No coding agent installed', { weight: 700 });
    text(c, x + P + 24 + GAP.item, cy + L + GAP.caption + L / 2, 'Claude Code or Codex writes it for you.', { fill: t.dim, size: SIZE.small });
    cy += 2 * L + GAP.caption + GAP.group;
    caption('Install one');
    ['omarchy default agent claude', 'omarchy default agent codex'].forEach((cmd, i) => {
      if (i) cy += GAP.item;
      c.group('CommandRow ' + cmd, () => {
        fillBox(c, x + P, cy, inner, BOX.control, 0.04); border(c, x + P, cy, inner, BOX.control, { stroke: t.fg, so: 0.25 });
        text(c, x + P + PAD.control, cy + BOX.control / 2, '$ ' + cmd, { fill: t.fg });
        button(c, { x: x + w - P - GAP.icon - BOX.row, y: cy + (BOX.control - BOX.row) / 2, w: BOX.row, h: BOX.row, variant: 'ghost', icon: 'copy', id: 'Copy' });
      });
      cy += BOX.control;
    });
    cy += GAP.item;
    text(c, x + P, cy + L / 2, 'Reopen this menu once one is installed.', { fill: t.dim, size: SIZE.small });
    cy += L;
  } else {
    // AGENT: only installed ones, the Omarchy default named
    caption('Agent', 'claude is the Omarchy default');
    segmented(c, { x: x + P, y: cy, w: inner, stretch: true, id: 'AgentPicker', items: [
      { icon: 'robot', label: 'Claude Code', selected: agent === 'claude' }, { icon: 'robot', label: 'Codex', selected: agent === 'codex' }] });
    cy += BOX.control + GAP.group;
    // MODEL: the names the CLI uses, the exact id at the right, tick on the chosen one
    caption('Model', 'from ' + (agent === 'claude' ? 'claude --help' : 'codex debug models'));
    A.models.forEach(([id, name, detail], i) => {
      const sel = id === model, hover = o.hover !== undefined ? o.hover === i : false;
      c.group('ModelRow/' + (sel ? 'selected' : hover ? 'hover' : 'normal') + ' ' + name, () => {
        if (hover) fillBox(c, x + P, cy, inner, rh, 0.08);
        text(c, x + P + PAD.control, cy + rh / 2, name, { fill: sel || hover ? t.accent : t.fg, weight: sel ? 700 : 400 });
        text(c, x + w - P - PAD.control - BOX.icon - GAP.icon, cy + rh / 2, detail, { fill: t.dim, size: SIZE.small, anchor: 'end' });
        if (sel) icon(c, 'check', x + w - P - PAD.control - BOX.icon, cy + (rh - BOX.icon) / 2, BOX.icon, { fill: t.accent });
      });
      cy += rh;
    });
    cy += GAP.cluster;
    // a custom model name is the exception: a text button, the field only once asked for
    if (o.otherOpen) field(c, { x: x + P, y: cy, w: inner, state: 'focus', value: o.otherValue || '', placeholder: 'Model name, as ' + A.bin + ' ' + A.modelFlag + ' takes it', id: 'OtherModel' });
    else button(c, { x: x + P, y: cy, variant: 'ghost', label: 'Other model…', id: 'OtherModelButton' });
    cy += BOX.control + GAP.group;
    // REASONING: an ordered scale, so a track with stops instead of a list (40: stops 12 down, labels 20 under them)
    caption('Reasoning', 'more thinking, slower answer');
    c.group('LevelTrack', () => {
      const n = levels.length, inset = 24, span = inner - 2 * inset, step = span / (n - 1), tx = x + P + inset, ty = cy + 12;
      const si = Math.max(0, levels.indexOf(effort));
      hairline(c, tx, ty, span, { fo: 0.3 });
      if (si > 0) rect(c, tx, ty - 0.5, step * si, 2, { fill: t.accent, fo: 0.7 });
      levels.forEach((lv, i) => {
        const cx = tx + step * i, sel = i === si;
        c.group('Stop/' + (sel ? 'selected' : 'normal') + ' ' + (lv || 'default'), () => {
          if (sel) { circle(c, cx, ty, 7, { fill: t.accent, fo: 0.25 }); circle(c, cx, ty, 4.5, { fill: t.accent }); }
          else { circle(c, cx, ty, 4, { fill: t.bg }); circle(c, cx, ty, 4, { fill: 'none', stroke: t.fg, so: i < si ? 0.9 : 0.5, sw: 1.5 }); }
          text(c, cx, ty + 20, lv ? cap(lv) : 'Default', { size: SIZE.caption, weight: sel ? 700 : 400, fill: sel ? t.accent : t.dim, anchor: 'middle' });
        });
      });
    });
    cy += 40 + SEP;
    hairline(c, x + P, cy, inner, { fo: 0.15 }); cy += SEP;
    button(c, { x: x + P, y: cy, w: inner, icon: 'sparkle', label: 'Generate now  Ctrl+G', variant: 'primary', px: PAD.primary, id: 'GenerateNow' });
    cy += BOX.control;
  }
  const h = cy + P - y;
  c.insertAt(bgAt, () => { rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 }); });
  c.add('</g>');
  return h;
}

// The keys panel: 16 padding, the title on a 24 row, 40 px rows 4 apart.
function keybindingsPanel(c, x, y, w, h) {
  const t = T(), P = PAD.dialog, rh = 40;
  c.group('KeybindingsPanel', () => {
    rect(c, x, y, w, h, { fill: t.bg }); border(c, x, y, w, h, { stroke: t.accent, so: 1, sw: 2 });
    text(c, x + P, y + P + BOX.row / 2, 'Omagit keybindings…', { size: SIZE.title, fill: t.dim });
    let cy = y + P + BOX.row + GAP.group;
    const rows = [['CTRL + K', 'Keybindings', true], ['CTRL + 1', 'Changes'], ['CTRL + 2', 'History'], ['CTRL + 3', 'Diff'], ['CTRL + ENTER', 'Commit'], ['CTRL + G', 'Generate message'], ['CTRL + P', 'Pull'], ['CTRL SHIFT + P', 'Push'], ['CTRL + F', 'Fetch'], ['CTRL SHIFT + M', 'Merge']];
    const kx = x + P + 16, chx = kx + 152, vx = chx + BOX.icon + GAP.icon;
    rows.forEach(([k, v, hot]) => {
      if (cy + rh > y + h - P) return;
      if (hot) fillBox(c, x + P, cy, w - 2 * P, rh, 0.08);
      text(c, kx, cy + rh / 2, k, { size: SIZE.subtitle, weight: 500, fill: hot ? t.accent : t.fg });
      icon(c, 'chevronR', chx, cy + (rh - BOX.icon) / 2, BOX.icon, { fill: t.dim });
      text(c, vx, cy + rh / 2, v, { size: SIZE.subtitle, weight: 500, fill: hot ? t.accent : t.fg });
      cy += rh + GAP.cluster;
    });
  });
}

// ------------------------------------------------------------ screen composer
// The body stands BAR under the top bar's hairline and BAR over the footer's
// (the side margin over the window's edge when shallow), a side margin in from
// either edge; panes stand a margin apart.
function screen(o) {
  const { W, H } = o; const lv = o.level || levelFor(W); const shallow = H < SHALLOW;
  const d = density(W, H), m = d.margin;
  const c = new K.Canvas(o.id, W, H); const t = T();
  rect(c, 0, 0, W, H, { fill: t.bg, id: 'window' });
  const top = topBar(c, W, lv, { ...o, d, shallow });
  const footH = shallow ? 0 : footer(c, W, H, lv, { ...o, d });
  const bodyY = top + BAR, bodyH = (shallow ? H - m : H - footH - BAR) - bodyY;
  // the page's CHANGES and MESSAGE header rows fold away on a two-row bar and in a shallow window
  const folded = top > TOP_BAR || shallow;
  const page = o.page || 'changes', po = { ...o, shallow, folded, d };
  if (stacked(lv)) {
    const nx = m, nw = W - m * 2;
    if (page === 'changes') changesPage(c, nx, bodyY, nw, bodyH, lv, po);
    else if (page === 'history') historyPage(c, nx, bodyY, nw, bodyH, lv, po);
    else { // diff tab: rail + diff
      const rw = miniRail(c, nx, bodyY, bodyH);
      diffPane(c, nx + rw + m, bodyY, nw - rw - m, bodyH, lv, { split: false });
    }
  } else {
    const leftW = o.leftW || (lv === 'xl' ? 560 : lv === 'l' ? 400 : 340);
    if (o.mini) {
      const rw = miniRail(c, m, bodyY, bodyH);
      diffPane(c, m + rw + m, bodyY, W - m * 3 - rw, bodyH, lv, o);
    } else if (o.diffHidden) {
      if (page === 'changes') changesPage(c, m, bodyY, W - m * 2, bodyH, lv, po);
      else historyPage(c, m, bodyY, W - m * 2, bodyH, lv, po);
    } else {
      if (page === 'changes') changesPage(c, m, bodyY, leftW, bodyH, lv, po);
      else historyPage(c, m, bodyY, leftW, bodyH, lv, po);
      // splitter handle, centred in the pane gap
      rect(c, m + leftW + (m - 2) / 2, bodyY + bodyH / 2 - 16, 2, 32, { fill: t.fg, fo: 0.2, id: 'SplitterHandle' });
      diffPane(c, m + leftW + m, bodyY, W - m * 3 - leftW, bodyH, lv, o);
    }
  }
  // overlays: menus hang 4 under the top bar and clamp to the window's margins; under a two-row
  // bar (extra narrow) they hang 4 under their button's row, over the tabs, not a row away from it
  const menuY = (top > TOP_BAR ? BAR + BOX.control : TOP_BAR) + GAP.cluster, clampW = dw => Math.min(dw, W - 2 * m);
  // the branch chip's x: the menus and the New branch card hang from it
  const bx = c.anchors.BranchChip.x;
  if (o.overlay === 'branch') {
    // The last row starts a new branch (Ctrl+N). Typing a name no branch has
    // leaves it as the only row, carrying the name: Return opens the card with it.
    const q = o.query, hover = o.hover || 'feature/askpass';
    const items = q ? [{ type: 'search', label: 'Search branches…', value: q }, { label: 'New branch “' + q + '”…', icon: 'branchPlus', hover: true }] : [
      { type: 'search', label: 'Search branches…' }, { type: 'section', label: 'Local' },
      { label: 'main', icon: 'branch', checked: true }, { label: 'feature/askpass', icon: 'branch', hover: hover === 'feature/askpass', hint: '2 days ago' }, { label: 'feature/tiling', icon: 'branch', hint: 'today' },
      { type: 'section', label: 'Remote' }, { label: 'origin/main', icon: 'cloud' }, { label: 'origin/feature/askpass', icon: 'cloud' },
      { type: 'sep' }, { label: 'New branch…', icon: 'branchPlus', hint: 'Ctrl+N', hover: hover === 'new' }];
    menuCard(c, { x: bx, y: menuY, w: Math.min(300, W - m - bx), id: 'BranchMenu', items });
  }
  if (o.overlay === 'newBranch') { // under the branch chip, moved left to stay a margin inside the window
    const nb = o.newBranch || {}, cw = clampW(360), cx = Math.min(bx, W - m - cw);
    newBranchCard(c, { x: cx, y: menuY, w: cw, ...nb });
    if (nb.pickerOpen) { // the From picker's menu, the picker's width, 4 under it: branches and tags
      const pk = c.anchors.BasePicker;
      menuCard(c, { x: pk.x, y: pk.y + pk.h + GAP.cluster, w: pk.w, id: 'BaseMenu', items: [
        { type: 'search', label: 'Search branches and tags…' }, { type: 'section', label: 'Local' },
        { label: 'main', icon: 'branch', checked: true, hint: 'current' }, { label: 'feature/askpass', icon: 'branch', hover: true, hint: '2 days ago' }, { label: 'feature/tiling', icon: 'branch', hint: 'today' },
        { type: 'section', label: 'Remote' }, { label: 'origin/main', icon: 'cloud' }, { label: 'origin/feature/askpass', icon: 'cloud' },
        { type: 'section', label: 'Tags' }, { label: 'v0.4', icon: 'tag', hint: '8 days ago' }] });
    }
  }
  if (o.overlay === 'commitMenu') { // a commit's context menu, its corner on the pointer in the row
    const r = c.anchors.CommitRow, px = r.msgX + 96, py = r.y + r.h / 2;
    menuCard(c, { x: Math.min(px, W - m - 260), y: py, w: 260, id: 'CommitMenu', items: [
      { label: 'Copy SHA', icon: 'copy' }, { label: 'Copy short SHA', icon: 'copy' }, { label: 'Copy message', icon: 'copy' },
      { type: 'sep' }, { label: 'New branch from here…', icon: 'branchPlus', hint: 'Ctrl+N', hover: true }] });
  }
  if (o.overlay === 'repo') {
    menuCard(c, { x: m, y: menuY, w: clampW(300), id: 'RepoMenu', items: [
      { type: 'search', label: 'Search repositories…' }, { type: 'section', label: 'Recent' },
      { label: 'omagit', icon: 'folder', checked: true, hint: '~/Projects' }, { label: 'feelinmyskin-mobile', icon: 'folder', hint: '~/Projects' }, { label: 'dotfiles', icon: 'folder', hint: '~' },
      { type: 'sep' }, { label: 'Open…', icon: 'folderOpen', hint: 'Ctrl+O' }, { label: 'Clone…', icon: 'fetch', hint: 'Ctrl+Shift+O' }] });
  }
  if (o.overlay === 'sync') {
    menuCard(c, { x: W - m - clampW(260), y: menuY, w: clampW(260), id: 'SyncMenu', items: [
      { label: 'Pull', icon: 'pull', hint: '2 behind · Ctrl+P', hover: true }, { label: 'Push', icon: 'push', hint: '1 ahead · Ctrl+Shift+P' }, { label: 'Fetch', icon: 'fetch', hint: 'Ctrl+F' },
      { type: 'sep' }, { label: 'Merge…', icon: 'merge', hint: 'Ctrl+Shift+M' }] });
  }
  if (o.overlay === 'more' || o.overlay === 'filesView') {
    // Folded, on the Changes tab, the page's header rows' controls come first: the files view as a
    // submenu (its icon the current view's), the unversioned toggle, the agent settings (they hang
    // from More then); the Diff and History tabs have none of them. Then what folded into More on
    // an unstacked row, then More's own entries.
    const view = FILE_VIEWS.find(([id]) => id === filesView(lv, o));
    const pageItems = folded && page === 'changes' ? [
      { label: 'Files view', icon: view[1], submenu: true, hover: o.overlay === 'filesView' }, { label: 'Show unversioned files', icon: 'eye', checked: true },
      { label: 'Agent settings…', icon: 'cog' }, { type: 'sep' }] : [];
    const syncItems = stacked(lv) || lv !== 'm' ? [] : [{ label: 'Fetch', icon: 'fetch', hint: 'Ctrl+F' }, { label: 'Merge…', icon: 'merge', hint: 'Ctrl+Shift+M' }, { type: 'sep' }];
    // More ends the tabs' row on a two-row bar: its menu hangs 4 under the whole bar, under More
    const mw = clampW(240), mx = W - m - mw, moreY = top + GAP.cluster;
    menuCard(c, { x: mx, y: moreY, w: mw, id: 'MoreMenu', items: [...pageItems, ...syncItems,
      { label: 'Refresh', icon: 'refresh', hint: 'F5' }, { label: 'Open repository…', icon: 'folderOpen', hint: 'Ctrl+O' }, { label: 'Clone…', icon: 'fetch' }, { type: 'sep' },
      { label: 'Keybindings', icon: 'keyboard', hint: 'Ctrl+K' }, { label: 'Settings…', icon: 'cog', hint: 'Ctrl+,' }] });
    if (o.overlay === 'filesView') {
      // Qt's rule for a submenu at the screen's edges, with the window for the screen: beside the menu,
      // its first row level with the entry; on the other side where it has no room; over the menu, the
      // window's margin in, where neither side has. As wide as its rows.
      const items = FILE_VIEWS.map(([id, ic]) => ({ label: FILE_VIEW_NAMES[id], icon: ic, checked: id === view[0] }));
      const sw = 2 * PAD.menu + 2 * PAD.control + BOX.icon + GAP.icon + Math.max(...items.map(it => tw(it.label))) + GAP.icon + BOX.icon;
      const sx = mx + mw + sw <= W - m ? mx + mw : mx - sw >= m ? mx - sw : m;
      menuCard(c, { x: sx, y: moreY, w: sw, id: 'FilesViewMenu', items });
    }
  }
  if (o.overlay === 'options') { // opens upwards from the Options button, 4 over it
    // Amend alone, as in the app: check-all is the table header's box, the sparkle writes the message,
    // and the unversioned toggle folds into the top bar's More with the CHANGES row
    const opt = c.rec.find(r => r.id === 'Options');
    const items = [{ label: 'Amend last commit', icon: 'undo', hint: 'Ctrl+Shift+A' }];
    menuCard(c, { x: opt.x, y: opt.y - GAP.cluster - menuHeight(items), w: clampW(240), id: 'OptionsMenu', items });
  }
  if (o.overlay === 'merge') {
    const dw = clampW(640), dh = mergeDialogHeight(dw < 520);
    fillBox(c, 0, 0, W, H, 0.5, { fill: t.bg, id: 'Scrim' });
    mergeDialog(c, Math.round((W - dw) / 2), Math.round((H - dh) / 2), dw, { h: dh });
  }
  if (o.overlay === 'settings') { // centred over a scrim like the merge view, as wide as the window lets it
    const sw = clampW(SETTINGS_W), sh = settingsDialogHeight(sw, o.settings);
    fillBox(c, 0, 0, W, H, 0.5, { fill: t.bg, id: 'Scrim' });
    settingsDialog(c, Math.round((W - sw) / 2), Math.round((H - sh) / 2), sw, o.settings);
  }
  if (o.overlay === 'keys') {
    const kw = clampW(800), kh = Math.min(500, H - 2 * m);
    fillBox(c, 0, 0, W, H, 0.5, { fill: t.bg, id: 'Scrim' });
    keybindingsPanel(c, Math.round((W - kw) / 2), Math.round((H - kh) / 2), kw, kh);
  }
  if (o.overlay === 'commit') {
    const tile = c.anchors.CommitTile;
    const pw = Math.min(360, W - 2 * m - tile.w - GAP.item);
    commitPopover(c, { x: tile.x + tile.w + GAP.item, bottom: tile.y + tile.h, w: pw, message: o.message, messageBody: o.messageBody, amend: o.amend });
  }
  if (o.overlay === 'agent') {
    // folded pages have no cog: the settings hang from More, whose entry opens them
    const cog = c.rec.find(r => r.id === 'AgentSettings') || c.rec.find(r => r.id === 'More');
    const pw = clampW(360), ph = agentPopoverHeight((o.agent || {}).agent);
    // under the cog where the window has room for it, over it where it has
    // not (the commit page's cog stands low, over the message box), and
    // otherwise moved up to fit; right-aligned with the column the cog stands in
    const under = cog.y + cog.h + GAP.cluster, over = cog.y - GAP.cluster - ph, bottom = H - m;
    const py = under + ph <= bottom ? under : over >= m ? over : Math.max(m, bottom - ph);
    agentPopover(c, { x: Math.max(m, Math.min(cog.x + cog.w + GAP.icon, W - m) - pw), y: py, w: pw, ...(o.agent || {}) });
  }
  if (o.overlay === 'searchOptions') { // proposal D: 4 under the filter field, right-aligned with it
    const opt = c.rec.find(r => r.id === 'SearchOptions');
    const fieldRight = opt.x + opt.w + GAP.icon + BOX.icon + PAD.control, fieldBottom = opt.y + opt.h + (BOX.control - BOX.row) / 2;
    const mw = clampW(280);
    menuCard(c, { x: Math.max(m, fieldRight - mw), y: fieldBottom + GAP.cluster, w: mw, id: 'SearchOptionsMenu', items: [
      { type: 'section', label: 'Search in' },
      { label: 'Message', checked: true }, { label: 'Author and e-mail', checked: true }, { label: 'SHA', checked: true },
      { type: 'sep' }, { type: 'section', label: 'Matches' },
      { label: 'First 10,000', checked: true, hint: 'fast' }, { label: 'All', hint: 'slower in huge repos', hover: true }] });
  }
  return c;
}

module.exports = { screen, density, agentPopover, commitPopover, newBranchCard, HEAD_BASE, levelFor, topBar, syncDropdown, changesPage, changesTable, actionBar, historyPage, commitsTable, commitDetails, diffPane, miniRail, footer, menuCard, menuHeight, dialogCard, mergeDialog, mergeDialogHeight, settingsDialog, settingsDialogHeight, keybindingsPanel, FILES, COMMITS, DIFF, tokens, TOP_BAR, SHALLOW, TALL };
