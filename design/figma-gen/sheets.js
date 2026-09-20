// Foundations, Components, Layout rules and Cover sheets.
const K = require('./kit.js'); const S = require('./screens.js');
const { rect, border, fillBox, hairline, vline, text, icon, circle, button, segmented, field, checkbox, sectionLabel, dimText, refChip, statusPill, badge, tw, SIZE } = K;
const T = () => K.theme();

function caption(c, x, y, s) { text(c, x, y, s, { size: SIZE.small, fill: T().dim, id: 'caption ' + s }); }
function heading(c, x, y, s) { text(c, x, y, s, { size: SIZE.heading, weight: 500, id: 'heading ' + s }); }

function cover() {
  const t = T(), c = new K.Canvas('Cover · Omagit UI', 1200, 760);
  rect(c, 0, 0, 1200, 760, { fill: t.bg, id: 'bg' });
  icon(c, 'commit', 60, 56, 40, { fill: t.accent });
  text(c, 112, 76, 'Omagit', { size: 40, weight: 700 });
  text(c, 60, 130, 'Omarchy-native git UI · design system and tiling-aware layouts', { size: SIZE.heading, fill: t.dim });
  let y = 190;
  const principles = [
    ['One kit, one place', 'Every control is a component with the Omarchy state chrome: foreground-alpha fills (4 / 8 / 18 / 22 %), 1 px hairline borders, square corners, accent only for the chosen thing and the primary action.'],
    ['The window folds, it never breaks', 'Five width classes (XL ≥ 1400, L ≥ 1000, M ≥ 700, S ≥ 480, XS) and two height classes. Labels give way to icons, icons to menus, panes to tabs. Nothing is elided mid-word and nothing disappears without a home.'],
    ['Commit is always where you left it', 'The primary action lives in a fixed action bar at the bottom of the left section. Repo and branch, the Changes | History view toggle, and sync live in the top bar. Same places at every size, so muscle memory works in a quarter tile as in full screen.'],
    ['Diff adapts by itself', 'Split view above 720 px of pane width, unified below; Ctrl+T cycles Auto → Split → Unified. Narrow windows get a Diff tab with the mini rail so switching files never leaves the diff, and the rail\'s commit tile opens a popover so committing never leaves it either.'],
    ['Type and spacing follow shell.toml', 'Sizes are at base 12: caption 10, body 12, subtitle 13, title 14, heading 16. Every px in this file multiplies by base/12, exactly like Style.space() in omarchy-shell.'],
  ];
  principles.forEach(([h, b]) => {
    text(c, 60, y, h, { size: SIZE.title, weight: 700, fill: t.accent }); y += 22;
    // wrap body at ~100 chars
    const words = b.split(' '); let line = '';
    words.forEach(w => { if ((line + ' ' + w).length > 105) { text(c, 60, y, line, { fill: t.fg }); y += 18; line = w; } else line = line ? line + ' ' + w : w; });
    text(c, 60, y, line, { fill: t.fg }); y += 34;
  });
  y += 6;
  dimText(c, 60, y, 'Pages: Foundations · Components · Screens (Wide, Half & Quarter, Narrow) · Light theme · Layout rules', { size: SIZE.small });
  return c;
}

function foundations() {
  const t = T(), c = new K.Canvas('Foundations', 1840, 1180);
  rect(c, 0, 0, 1840, 1180, { fill: t.bg, id: 'bg' });
  heading(c, 40, 40, 'Colour · palette roles from colors.toml');
  let x = 40, y = 80;
  const roles = [['bg', 'background'], ['fg', 'foreground'], ['accent', 'accent'], ['dim', 'muted / dim'], ['red', 'red · deleted, conflict, urgent'], ['green', 'green · added'], ['blue', 'blue · modified'], ['magenta', 'magenta · renamed, remote'], ['yellow', 'yellow · tags, numbers'], ['cyan', 'cyan']];
  for (const themeName of ['dark', 'light']) {
    const th = K.THEMES[themeName];
    text(c, x, y, th.name, { weight: 700, size: SIZE.subtitle }); y += 22;
    let sx = x;
    roles.forEach(([k, label]) => {
      c.group(`Swatch/${themeName}/${k}`, () => {
        rect(c, sx, y, 96, 56, { fill: th[k] }); border(c, sx, y, 96, 56, { stroke: t.fg, so: 0.4 });
        text(c, sx, y + 68, k, { size: SIZE.small, weight: 700 }); text(c, sx, y + 84, th[k], { size: SIZE.caption, fill: t.dim });
      });
      sx += 108;
    });
    y += 110;
  }
  heading(c, 40, y + 10, 'State fills · foreground at alpha (Omarchy [controls])'); y += 44;
  let sx = 40;
  [['normal fill', 0.04], ['hover fill', 0.08], ['hairline', 0.12], ['selected fill', 0.18], ['pressed fill', 0.22], ['hover border', 0.25], ['selection', 0.35], ['normal border', 0.40], ['disabled text', 0.45]].forEach(([n, a]) => {
    c.group('Alpha/' + n, () => { fillBox(c, sx, y, 96, 40, a); border(c, sx, y, 96, 40, { stroke: t.fg, so: 0.4 }); text(c, sx, y + 54, n, { size: SIZE.small }); text(c, sx, y + 70, Math.round(a * 100) + ' %', { size: SIZE.caption, fill: t.dim }); });
    sx += 108;
  });
  y += 96;
  heading(c, 40, y + 10, 'Diff tints'); y += 44; sx = 40;
  [['removed row', t.red, 0.16], ['removed inline', t.red, 0.35], ['added row', t.green, 0.16], ['added inline', t.green, 0.35], ['filler', t.fg, 0.05], ['selected row', t.fg, 0.08]].forEach(([n, col, a]) => {
    c.group('Tint/' + n, () => { fillBox(c, sx, y, 120, 32, a, { fill: col }); text(c, sx, y + 46, n, { size: SIZE.small }); });
    sx += 132;
  });
  y += 70;
  heading(c, 40, y + 10, 'Type · JetBrains Mono (shell monospace), sizes at base 12'); y += 48;
  [['caption', 10, 700, 'SECTION LABEL · CAPTION 10 BOLD, dim, letter-spacing 0.8'], ['small', 11, 400, 'Small 11 · hints, dates, counts'], ['body', 12, 400, 'Body 12 · everything else: labels, rows, code'],
   ['subtitle', 13, 400, 'Subtitle 13 · commit headline in details'], ['title', 14, 700, 'Title 14 bold · branch name, panel search'], ['heading', 16, 500, 'Heading 16 medium · dialog titles, keybinding rows'], ['display', 24, 700, 'Display 24 · hero numbers']].forEach(([n, s, w, sample]) => {
    c.group('Type/' + n, () => { text(c, 40, y, n, { size: SIZE.small, fill: t.dim }); text(c, 140, y, sample, { size: s, weight: w, fill: n === 'caption' ? t.dim : t.fg, ls: n === 'caption' ? 0.8 : 0 }); });
    y += Math.max(26, s + 14);
  });
  heading(c, 1180, 80, 'Spacing & sizes'); let ly = 116;
  [['margin', '12 · window edge to content'], ['gap', '10 · between panes and rows'], ['control', '28 · buttons, fields, segmented'], ['row', '26 · table rows · 28 commits · 30 menu rows'], ['top bar', '40 · repo · branch | view toggle | sync'], ['action bar', '44 (28 controls + 8 above + 8 below)'], ['footer', '28'], ['dialog padding', '20 / 18'], ['menu padding', '6, 2 px accent border'], ['icon', '14 in controls · 12 chevrons · 16 verdicts · 24 hero'], ['badge', '14 high, 10 bold, accent on bg'], ['mini tile', '40 × 40, gap 6'], ['scale', 'every value × base/12 (shell.toml [font] base-size)']].forEach(([k, v]) => {
    text(c, 1180, ly, k, { size: SIZE.small, weight: 700 }); text(c, 1300, ly, v, { size: SIZE.small, fill: t.dim }); ly += 22;
  });
  heading(c, 1180, ly + 20, 'Icons · Material Design Icons (md-* glyphs)'); ly += 56;
  let ix = 1180; Object.keys(K.ICON).forEach((n, i) => { c.group('Icon/' + n, () => { icon(c, n, ix, ly, 16); text(c, ix + 22, ly + 8, n, { size: SIZE.caption, fill: t.dim }); }); ix += 105; if ((i + 1) % 6 === 0) { ix = 1180; ly += 28; } });
  return c;
}

function components() {
  const t = T(), c = new K.Canvas('Components', 1500, 1520);
  rect(c, 0, 0, 1500, 1520, { fill: t.bg, id: 'bg' });
  const caps = []; const cap = (x, y, s) => caps.push([x, y, s]);
  c.add('<g id="Components">');
  let x = 40, y = 70;
  cap(40, 40, 'Button · default / primary / ghost / danger · states normal, hover, pressed, selected, disabled');
  ['normal', 'hover', 'pressed', 'selected', 'disabled'].forEach(st => { x += button(c, { x, y, icon: 'pull', label: 'Pull', state: st, id: `Button/default/${st}` }) + 16; });
  x += 16; ['normal', 'hover', 'disabled'].forEach(st => { x += button(c, { x, y, icon: 'commit', label: 'Commit 5 files', variant: 'primary', state: st, px: 16, id: `Button/primary/${st}` }) + 16; });
  x += 16; ['normal', 'hover'].forEach(st => { x += button(c, { x, y, icon: 'sparkle', label: 'Generate', variant: 'ghost', state: st, id: `Button/ghost/${st}` }) + 16; });
  x += button(c, { x, y, icon: 'close', label: 'Abort merge', variant: 'danger', id: 'Button/danger/normal' }) + 16;
  y += 50; x = 40; cap(40, y - 14, 'Icon buttons · with badge · busy · with mark · dropdown');
  ['normal', 'hover', 'selected', 'disabled'].forEach(st => { x += button(c, { x, y, w: 28, icon: 'refresh', state: st, id: `Button/icon/${st}` }) + 12; });
  x += 8; x += button(c, { x, y, icon: 'pull', label: 'Pull', badge: 2, id: 'Button/badge' }) + 24;
  x += button(c, { x, y, icon: 'loader', label: 'Fetching…', busy: true, id: 'Button/busy' }) + 16;
  x += button(c, { x, y, icon: 'merge', label: 'Merge', mark: true, id: 'Button/mark' }) + 16;
  x += button(c, { x, y, icon: 'split', label: 'Split', chevron: true, id: 'Button/dropdown' }) + 16;
  x += button(c, { x, y, variant: 'ghost', icon: 'branch', label: 'main', chevron: true, weight: 700, iconFill: t.accent, id: 'Chip/branch' });
  c.add('<g id="Chip/branch/accent-label">'); text(c, x - K.measureButton({ icon: 'branch', label: 'main', chevron: true }) + 30, y + 14, 'main', { weight: 700, fill: t.accent }); c.add('</g>'); x += 16;
  x += button(c, { x, y, variant: 'ghost', icon: 'folderOpen', label: 'omagit', chevron: true, id: 'Chip/repo' }) + 16;
  y += 50; x = 40; cap(40, y - 14, 'Segmented control · labels + count · stretch · icons only');
  x += segmented(c, { x, y, id: 'Segmented/labels', items: [{ icon: 'commit', label: 'Changes', count: 7, selected: true }, { icon: 'history', label: 'History' }] }) + 24;
  x += segmented(c, { x, y, w: 360, stretch: true, id: 'Segmented/stretch', items: [{ icon: 'commit', label: 'Changes', count: 7 }, { icon: 'diff', label: 'Diff', selected: true }, { icon: 'history', label: 'History' }] }) + 24;
  x += segmented(c, { x, y, w: 180, stretch: true, id: 'Segmented/icons', items: [{ icon: 'commit', count: 7, selected: true }, { icon: 'diff' }, { icon: 'history' }] }) + 24;
  c.group('SyncDropdown', () => { fillBox(c, x, y, 92, 28, 0.04); border(c, x, y, 92, 28, { stroke: t.fg, so: 0.4 }); icon(c, 'down', x + 8, y + 7, 14); text(c, x + 24, y + 14, '2', { weight: 700, fill: t.accent }); icon(c, 'up', x + 38, y + 7, 14); text(c, x + 54, y + 14, '1', { weight: 700, fill: t.accent }); icon(c, 'chevron', x + 68, y + 8, 12, { opacity: 0.7 }); });
  y += 50; x = 40; cap(40, y - 14, 'Fields · normal, focus, search, with value');
  x += field(c, { x, y, w: 220, placeholder: 'Commit message', id: 'Field/normal' }) + 16;
  x += field(c, { x, y, w: 220, value: 'feature/til', state: 'focus', id: 'Field/focus' }) + 16;
  x += field(c, { x, y, w: 260, icon: 'search', placeholder: 'Filter by message, author or SHA', id: 'Field/search' }) + 16;
  x += field(c, { x, y, w: 200, icon: 'link', value: 'git@github.com:a/b.git', id: 'Field/value' }) + 16;
  y += 50; x = 40; cap(40, y - 14, 'Checkbox · off, checked, partial, disabled · Section label (with count) · Dim text');
  x += checkbox(c, { x, y: y + 7, checked: false, label: 'Amend last commit' }) + 24;
  x += checkbox(c, { x, y: y + 7, checked: true, label: 'Show unversioned' }) + 24;
  x += checkbox(c, { x, y: y + 7, checked: 'partial', label: 'All' }) + 24;
  x += checkbox(c, { x, y: y + 7, checked: false, disabled: true, label: 'Amend (merging)' }) + 40;
  x += sectionLabel(c, x, y + 14, 'Changes · 5/7', { id: 'Section/Changes' }) + 30;
  x += text(c, x, y + 14, 'Nothing to commit', { fill: t.dim, size: SIZE.small, id: 'DimText/hint' }) + 30;
  y += 50; x = 40; cap(40, y - 14, 'Ref chips · Status pills · Badge');
  [['main', 'head'], ['feature/askpass', 'local'], ['origin/main', 'remote'], ['v0.4', 'tag']].forEach(([l, k]) => { x += refChip(c, x, y + 6, l, k) + 12; });
  x += 20; ['M', 'A', 'D', 'R', 'C', '?'].forEach(s => { x += statusPill(c, x, y + 6, s) + 8; });
  x += 20; badge(c, x + 20, y + 7, 2); x += 40; badge(c, x + 30, y + 7, 128);
  y += 50; cap(40, y - 14, 'Tables · changes header + rows (normal, selected, untracked, conflict) · commit rows with graph');
  S.changesTable(c, 40, y, 560, 26 * 5 + 30, 'xl', { rows: [S.FILES[0], { ...S.FILES[2] }, { ...S.FILES[3], selected: false }, { ...S.FILES[2], st: 'C', name: 'DiffPane.cpp', selected: false }, S.FILES[5]] });
  S.commitsTable(c, 620, y, 840, 28 * 4 + 30, 'xl', {});
  y += 26 * 5 + 30 + 40; cap(40, y - 14, 'Diff · split & unified rows: context, removed, added, filler, header, inline change');
  S.diffPane(c, 40, y, 1420, 260, 'xl', { split: true });
  y += 260 + 40; cap(40, y - 14, 'Mini rail tiles · Menu card · Dialog card · Verdict · Footer · Top bar');
  S.miniRail(c, 40, y, 320);
  S.menuCard(c, { x: 100, y, w: 280, id: 'MenuCard', items: [{ type: 'search', label: 'Search branches…' }, { type: 'section', label: 'Local' }, { label: 'main', icon: 'branch', checked: true }, { label: 'feature/askpass', icon: 'branch', hover: true, hint: '2 days ago' }, { type: 'sep' }, { label: 'Open…', icon: 'folderOpen', hint: 'Ctrl+O' }, { label: 'Disabled row', icon: 'cloud', disabled: true }] });
  S.mergeDialog(c, 400, y, 640, { h: 300 });
  c.group('Tooltip', () => { rect(c, 1070, y, 200, 26, { fill: t.bg }); border(c, 1070, y, 200, 26, { stroke: t.fg, so: 1 }); text(c, 1080, y + 13, 'Pull (Ctrl+P) · 2 behind', { size: SIZE.small }); });
  c.group('StatusLine/busy', () => { icon(c, 'loader', 1070, y + 40, 14, { fill: t.accent }); text(c, 1090, y + 47, 'Fetching origin…', { size: SIZE.small, fill: t.dim }); });
  c.group('StatusLine/error', () => { icon(c, 'warn', 1070, y + 66, 14, { fill: t.red }); text(c, 1090, y + 73, 'Push rejected: fetch first', { size: SIZE.small, fill: t.red }); });
  c.group('Scrollbar', () => { rect(c, 1300, y, 8, 120, { fill: t.fg, fo: 0.03 }); rect(c, 1302, y + 10, 4, 40, { fill: t.fg, fo: 0.25 }); });
  c.group('SplitterHandle', () => { rect(c, 1330, y + 40, 2, 32, { fill: t.fg, fo: 0.2 }); });
  c.group('Keycap', () => { const k = 'CTRL + K'; const w = tw(k, 10) + 12; fillBox(c, 1360, y, w, 20, 0.08); border(c, 1360, y, w, 20, { stroke: t.fg, so: 0.4 }); text(c, 1366, y + 10, k, { size: 10, weight: 700, fill: t.dim }); });
  y += 340;
  c.add('</g>');
  c.add('<g id="Captions">'); caps.forEach(([cx, cy, s]) => caption(c, cx, cy, s)); c.add('</g>');
  return c;
}

function layoutRules() {
  const t = T(), c = new K.Canvas('Layout rules', 1500, 1000);
  rect(c, 0, 0, 1500, 1000, { fill: t.bg, id: 'bg' });
  heading(c, 40, 40, 'Layout rules · width classes (px at base 12, × base/12)');
  const cols = [['Class', 110], ['Width', 150], ['Body', 200], ['Top bar', 300], ['View toggle', 200], ['Changes table', 180], ['Diff', 180], ['Action bar', 170]];
  const rows = [
    ['XL', '≥ 1400', 'left pane 560 + diff', 'repo · branch | toggle | Pull Push Fetch Merge with labels · mini · diff', 'Changes 7 | History, centred; Refresh in the CHANGES row', 'name path status +− size', 'split, labels', 'Amend · Unversioned · All | Generate · Commit'],
    ['L', '1000–1399', 'left pane 400 + diff', 'same, sync icons + badges', 'same', 'name path status +−', 'split ≥ 720 else unified', 'All · Unversioned | ✦ · Commit; Amend in row'],
    ['M', '700–999', 'left pane 340 + diff', 'sync icons; Fetch, Merge → ⋯ menu', 'same', 'name path St', 'unified, [‹][›] 1/2 … ⋯', 'Amend | ✦ · Commit'],
    ['S', '480–699', 'single column, tabs', 'repo icon · branch | toggle | ↓2 ↑1 ▾ · ⋯', 'Changes 7 | Diff | History, centred', 'name + dim path, St', 'Diff tab: rail + unified', '⋯ · ✦ · Commit (stretch)'],
    ['XS', '< 480', 'single column, tabs', 'repo icon · branch | toggle | ↓2 ↑1 ▾ · ⋯', 'icons + count, centred', 'name + dim path, St', 'rail + unified', '⋯ · Commit (stretch)'],
  ];
  let y = 80, x = 40;
  cols.forEach(([n, w]) => { text(c, x, y, n, { size: 10, weight: 700, fill: t.dim, ls: 0.8 }); x += w; }); y += 10; hairline(c, 40, y, 1440, { fo: 0.2 }); y += 20;
  rows.forEach(r => { x = 40; r.forEach((v, i) => { const w = cols[i][1]; const maxc = Math.floor((w - 10) / 6.6); let s = String(v); const lines = []; while (s.length > maxc) { let cut = s.lastIndexOf(' ', maxc); if (cut < 10) cut = maxc; lines.push(s.slice(0, cut)); s = s.slice(cut).trim(); } lines.push(s); lines.forEach((l, j) => text(c, x, y + j * 16, l, { size: SIZE.small, weight: i === 0 ? 700 : 400, fill: i === 0 ? t.accent : t.fg })); x += w; }); y += 60; hairline(c, 40, y - 14, 1440, { fo: 0.1 }); });
  y += 10; heading(c, 40, y, 'Height classes'); y += 34;
  [['H ≥ 560', 'normal: footer with status + keys, message box 96 / 84 / 68 by width class, options row'], ['H < 560 (shallow)', 'footer hidden (status becomes a transient line in the action bar), message box one line that grows on focus up to 40 % of the pane, options fold into ⋯'], ['H < 400', 'history details collapse to one line']].forEach(([k, v]) => { text(c, 40, y, k, { weight: 700, fill: t.accent }); text(c, 220, y, v, { size: SIZE.small }); y += 24; });
  y += 16; heading(c, 40, y, 'Folding order (what gives way first)'); y += 34;
  ['1. Button labels → icons (sync group, diff toolbar), never truncated mid-word', '2. Fetch, Merge → ⋯ menu; Whitespace, Syntax → diff ⋯ menu', '3. Table columns: Size → +− → Status → Path (path joins the name as a dim suffix)', '4. Split diff → unified (pane < 720)', '5. Two panes → tabs (window < 700); Docked / Mini toggles vanish, the rail lives in the Diff tab', '6. Pull, Push → one sync dropdown carrying both counts; view toggle labels → icons when it would touch either group', '7. Repo chip → icon (34 px; it never folds away, it is the anchor that says which project this is)', '8. Footer → hidden (shallow); options → ⋯; commit button stretches'].forEach(l => { text(c, 40, y, l, { size: SIZE.body }); y += 20; });
  y += 16; heading(c, 40, y, 'Always true'); y += 34;
  ['Commit (Ctrl+Enter) is visible and reachable at every size: bottom-right (or full width) of the left section; in the Mini layout and the Diff tab it is the rail\'s bottom tile, which opens the commit popover.', 'Repo and branch are in the top-left, the view toggle is centred, sync is in the top-right. Keybindings (Ctrl+K) reach everything the mouse can.', 'Menus and dialogs clamp to the window: width = min(design width, window − 24), never off-screen.', 'Selection stays put across refresh, resize and layout changes; the selected file follows into the Diff tab and the rail.'].forEach(l => { text(c, 40, y, '·  ' + l, { size: SIZE.body }); y += 20; });
  // mini diagram: three window silhouettes
  const dx = 1000, dy = 560;
  [['XL / L / M', 300, 180, true], ['S / XS', 130, 180, false]].forEach(([n, w, h, side], i) => {
    const ox = dx + i * 340;
    c.group('Diagram/' + n, () => {
      border(c, ox, dy, w, h, { stroke: t.fg, so: 0.4 }); fillBox(c, ox, dy, w, 18, 0.08); text(c, ox + 6, dy + 9, w >= 200 ? 'repo · branch   toggle   sync' : 'repo·br  tog  sync', { size: 8, fill: t.dim });
      if (side) { border(c, ox + 8, dy + 26, 100, h - 34, { stroke: t.accent, so: 0.8 }); border(c, ox + 116, dy + 26, w - 124, h - 34, { stroke: t.fg, so: 0.4 }); text(c, ox + 14, dy + 40, 'changes', { size: 9 }); text(c, ox + 122, dy + 40, 'diff', { size: 9 }); fillBox(c, ox + 8, dy + h - 30, 100, 20, 0.18, { fill: t.accent }); text(c, ox + 14, dy + h - 20, 'commit', { size: 9, fill: t.accent }); }
      else { fillBox(c, ox + 8, dy + 26, w - 16, 14, 0.18); text(c, ox + 12, dy + 33, 'chg | diff | hist', { size: 8 }); border(c, ox + 8, dy + 46, w - 16, h - 54, { stroke: t.accent, so: 0.8 }); fillBox(c, ox + 8, dy + h - 30, w - 16, 20, 0.18, { fill: t.accent }); text(c, ox + 14, dy + h - 20, 'commit', { size: 9, fill: t.accent }); }
      text(c, ox, dy + h + 16, n, { size: SIZE.small, fill: t.dim });
    });
  });
  return c;
}

module.exports = { cover, foundations, components, layoutRules };
