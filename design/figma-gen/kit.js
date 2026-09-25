// Omagit design kit — SVG builders shared by every frame. Coordinates are
// absolute inside a frame; every builder returns the width it used.
const mdi = require('@mdi/js');

const THEMES = {
  dark:  { name: 'Moodpeak dark',  mode: 'dark',  bg: '#181c22', fg: '#e0e6ed', accent: '#4ecdc4', dim: '#8791a6',
           red: '#ff7b92', green: '#4ecdc4', yellow: '#82eeff', blue: '#6a85ff', magenta: '#b388eb', cyan: '#c9aff0' },
  light: { name: 'Moodpeak light', mode: 'light', bg: '#fafafa', fg: '#212121', accent: '#3264eb', dim: '#757575',
           red: '#c900c4', green: '#4a2fd0', yellow: '#026fde', blue: '#3264eb', magenta: '#8a4ad7', cyan: '#0c67de' },
};
let T = THEMES.dark;
function setTheme(name) { T = THEMES[name]; return T; }
function theme() { return T; }

const FONT = 'JetBrains Mono';
const SIZE = { caption: 10, small: 11, body: 12, subtitle: 13, title: 14, heading: 16, display: 24 };

// ---------------------------------------------------------------- grid
// Every size and gap is a multiple of GRID (at base 12; the app rounds the
// unit once, round(4 × base / 12), and multiplies it, so sums stay on the grid
// at any text size). 2 is left for optical nudges and 1 for hairlines; borders
// and hairlines are drawn inside the box they belong to, never added to it.
// Text sizes stay off the grid: text is centred in on-grid rows and controls.
const GRID = 4;
const SP = [4, 8, 12, 16, 24]; // the spacing scale
// Sizes.
const BOX = {
  control: 28, // buttons, fields, segmented controls
  row: 24,     // every clickable row (files, commits, menus, models), table headers, section header rows
  line: 16,    // a line of text: captions, code, message and body copy
  icon: 16,    // an icon's box; the glyph fills 7/8 of it (14 in 16)
  chevron: 12,
  check: 16,   // checkbox
  pill: 16,    // status pills, ref chips, counts inside a control
  badge: 12,   // counts hanging off a button's corner
  tile: 40,    // Mini rail tiles
  footer: 28,
  divider: 16, // a vertical divider between groups in a row
};
// Padding inside an element follows its size and is the same in every window.
const PAD = {
  pill: 4,     // 16 px elements: pills, chips, badges (2 in the 12 px badge)
  control: 8,  // 24–28 px elements: buttons, fields, rows, cells, menu items
  primary: 16, // the primary action of a surface (Commit, Merge, Generate now)
  big: 12,     // 36 px controls (the merge dialog's branch pickers)
  menu: 4,     // around a menu's rows
  popover: 12, // popovers and cards
  dialog: 16,
};
// Gaps inside a group, the same in every window.
const GAP = {
  icon: 4,     // an icon box (or chevron) to its label; the box's own air makes it read as 6
  cluster: 4,  // bordered controls that act as one: Prev|Next, toggles, repo·branch, tiles, chips
  check: 8,    // a checkbox to its label
  item: 8,     // separate controls in a row (sync buttons, Cancel · Merge), a control to its text
  group: 16,   // groups in a row; a divider stands at the start of the right half (8 | 8)
  caption: 4,  // a caption line (popovers, dialogs) to its content
  header: 8,   // a 24 px section header row to its content
  controlRow: 4, // a 28 px control row (diff toolbar, history filter) to its content: 24 + 8 = 28 + 4
};
// Spacing between things steps with the window, one step per class: the side
// margins and pane gaps with its width, the block gap with its height. Blocks
// are the parts stacked in a pane, sections included: a section's 24 px header
// row carries its own air above the label, so it needs no bigger gap.
const DENSITY = {
  compact:     { margin: 8,  block: 4 },
  regular:     { margin: 12, block: 8 },
  comfortable: { margin: 16, block: 12 },
};
// Chrome bars keep the same padding everywhere: 8 + 28 + 8 = 44, the top bar
// and the action row alike; the body stands 8 below the top bar's hairline
// and 8 above the footer's. (4 would push the sync badges, which hang 4 over
// the buttons' top edge, out of the window.)
const BAR = 8;
const CW = 0.6; // JetBrains Mono advance width / font size
const tw = (s, size = SIZE.body) => Math.round(String(s).length * CW * size);
const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const ICON = {
  commit: 'mdiSourceCommit', history: 'mdiHistory', pull: 'mdiDownload', push: 'mdiUpload', fetch: 'mdiCloudDownloadOutline',
  merge: 'mdiSourceMerge', branch: 'mdiSourceBranch', refresh: 'mdiRefresh', split: 'mdiViewSplitVertical',
  pilcrow: 'mdiFormatPilcrow', code: 'mdiCodeTags', up: 'mdiArrowUp', down: 'mdiArrowDown', chevron: 'mdiChevronDown',
  chevronR: 'mdiChevronRight', chevronL: 'mdiChevronLeft', folder: 'mdiFolder', folderOpen: 'mdiFolderOpen',
  search: 'mdiMagnify', sparkle: 'mdiCreation', cog: 'mdiCog', info: 'mdiInformationOutline', dockRight: 'mdiDockRight',
  mini: 'mdiViewCompactOutline', dots: 'mdiDotsHorizontal', check: 'mdiCheck', close: 'mdiClose', swap: 'mdiSwapHorizontal',
  keyboard: 'mdiKeyboardOutline', diff: 'mdiFileCompare', alert: 'mdiAlert', checkCircle: 'mdiCheckCircle', github: 'mdiGithub',
  stop: 'mdiStop', file: 'mdiFileOutline', back: 'mdiArrowLeft', minus: 'mdiMinus', plus: 'mdiPlus', tag: 'mdiTagOutline',
  cloud: 'mdiCloudOutline', robot: 'mdiRobotOutline', link: 'mdiLinkVariant', lock: 'mdiLockOutline', open: 'mdiOpenInNew',
  undo: 'mdiUndo', copy: 'mdiContentCopy', trash: 'mdiTrashCanOutline', pencil: 'mdiPencilOutline', star: 'mdiStarOutline',
  circle: 'mdiCircleMedium', unified: 'mdiViewSequentialOutline', auto: 'mdiAutoFix', sync: 'mdiSync', warn: 'mdiAlertCircleOutline',
  play: 'mdiPlay', filter: 'mdiFilterVariant', loader: 'mdiLoading',
  table: 'mdiTable', list: 'mdiFormatListBulleted', tree: 'mdiFileTree', folderOutline: 'mdiFolderOutline', eye: 'mdiEyeOutline',
  infinity: 'mdiInfinity', tune: 'mdiTuneVariant',
};
for (const [k, v] of Object.entries(ICON)) if (!mdi[v]) throw new Error('missing icon ' + k + ' ' + v);

class Canvas {
  constructor(id, w, h) { this.id = id; this.w = w; this.h = h; this.parts = []; this.rec = []; }
  // Manifest of component placements (kind, rect, props) used to swap the flat
  // SVG groups for component instances in Figma.
  record(e) { this.rec.push(e); }
  add(s) { this.parts.push(s); }
  // Draws fn's parts at an earlier index: a card's background once its height is known.
  insertAt(i, fn) { const rest = this.parts.splice(i); fn(); this.parts.push(...rest); }
  group(id, fn) { this.add(`<g id="${esc(id)}">`); fn(); this.add('</g>'); }
  svg() {
    return `<svg xmlns="http://www.w3.org/2000/svg" id="${esc(this.id)}" width="${this.w}" height="${this.h}" viewBox="0 0 ${this.w} ${this.h}">\n${this.parts.join('\n')}\n</svg>`;
  }
}

// ---------------------------------------------------------------- primitives
function rect(c, x, y, w, h, o = {}) {
  const a = [`x="${x}" y="${y}" width="${Math.max(0, w)}" height="${Math.max(0, h)}"`];
  if (o.id) a.push(`id="${esc(o.id)}"`);
  a.push(o.fill ? `fill="${o.fill}"` : 'fill="none"');
  if (o.fo !== undefined) a.push(`fill-opacity="${o.fo}"`);
  if (o.stroke) a.push(`stroke="${o.stroke}" stroke-width="${o.sw || 1}"`);
  if (o.so !== undefined) a.push(`stroke-opacity="${o.so}"`);
  if (o.rx) a.push(`rx="${o.rx}"`);
  c.add(`<rect ${a.join(' ')}/>`);
}
// 1px crisp inset border
function border(c, x, y, w, h, o = {}) {
  const sw = o.sw || 1, i = sw / 2;
  rect(c, x + i, y + i, w - sw, h - sw, { ...o, sw });
}
function fillBox(c, x, y, w, h, fo, o = {}) { rect(c, x, y, w, h, { fill: o.fill || T.fg, fo, ...o }); }
function hairline(c, x, y, w, o = {}) { rect(c, x, y, w, 1, { fill: o.fill || T.fg, fo: o.fo ?? 0.12, id: o.id }); }
function vline(c, x, y, h, o = {}) { rect(c, x, y, 1, h, { fill: o.fill || T.fg, fo: o.fo ?? 0.12, id: o.id }); }
function text(c, x, y, str, o = {}) {
  const size = o.size || SIZE.body, weight = o.weight || 400;
  const a = [`x="${x}" y="${(y + size * 0.36).toFixed(1)}"`, `font-family="${FONT}"`, `font-size="${size}"`, `font-weight="${weight}"`,
    `fill="${o.fill || T.fg}"`];
  if (o.id) a.push(`id="${esc(o.id)}"`);
  if (o.opacity !== undefined) a.push(`fill-opacity="${o.opacity}"`);
  if (o.anchor) a.push(`text-anchor="${o.anchor}"`);
  if (o.ls) a.push(`letter-spacing="${o.ls}"`);
  c.add(`<text ${a.join(' ')} xml:space="preserve">${esc(str)}</text>`);
  return tw(str, size);
}
// An icon in a box of `box` px: the MDI glyph (drawn on its 24 grid) fills
// 7/8 of the box, centred — 14 in the 16 box, as the Figma Icon set has it.
const GLYPH = 14 / 16;
function icon(c, name, x, y, box = BOX.icon, o = {}) {
  const d = mdi[ICON[name]]; if (!d) throw new Error('icon ' + name);
  const g = box * GLYPH, off = (box - g) / 2, k = (g / 24).toFixed(4);
  const op = o.opacity !== undefined ? ` fill-opacity="${o.opacity}"` : '';
  c.add(`<g id="${esc(o.id || 'icon/' + name)}" transform="translate(${+(x + off).toFixed(2)},${+(y + off).toFixed(2)}) scale(${k})"><path d="${d}" fill="${o.fill || T.fg}"${op}/></g>`);
  return box;
}
function circle(c, cx, cy, r, o = {}) {
  const a = [`cx="${cx}" cy="${cy}" r="${r}"`, `fill="${o.fill || T.fg}"`];
  if (o.fo !== undefined) a.push(`fill-opacity="${o.fo}"`);
  if (o.stroke) a.push(`stroke="${o.stroke}" stroke-width="${o.sw || 1}"`);
  if (o.id) a.push(`id="${esc(o.id)}"`);
  c.add(`<circle ${a.join(' ')}/>`);
}

// ---------------------------------------------------------------- controls
// State chrome shared by every control: fills are foreground alpha, borders too.
const STATE = {
  normal:   { fo: 0.04, so: 0.40 },
  hover:    { fo: 0.08, so: 0.25 },
  pressed:  { fo: 0.22, so: 0.25 },
  focus:    { fo: 0.08, so: 0.25 },
  selected: { fo: 0.18, so: 0.18, textAccent: true, bold: true },
  disabled: { fo: 0.0,  so: 0.12, textOpacity: 0.45 },
};

// A count hanging off a corner: `right` / `top` are the badge's own edges.
function badge(c, right, top, n, o = {}) {
  const s = String(n), h = BOX.badge, w = Math.max(h, tw(s, 10) + 2 * 2);
  rect(c, right - w, top, w, h, { fill: o.fill || T.accent, id: 'badge' });
  text(c, right - w / 2, top + h / 2, s, { size: 10, weight: 700, fill: T.bg, anchor: 'middle' });
  return w;
}

// [px][icon box][4][label][4][chevron box][px]
function measureButton(o) {
  const px = o.px ?? PAD.control, box = o.iconBox || BOX.icon;
  let w = px * 2;
  if (o.icon) w += box;
  if (o.icon && o.label) w += GAP.icon;
  if (o.label) w += tw(o.label, o.size || SIZE.body);
  if (o.chevron) w += GAP.icon + BOX.chevron;
  return o.w || Math.max(w, o.minW || 0);
}

// variant: default | primary | ghost | danger
function button(c, o) {
  const { x, y } = o, h = o.h || BOX.control, variant = o.variant || 'default', st = STATE[o.state || 'normal'];
  const w = measureButton(o), px = o.px ?? PAD.control, box = o.iconBox || BOX.icon, size = o.size || SIZE.body;
  const id = o.id || `Button/${variant}/${o.state || 'normal'}${o.label ? ' ' + o.label : ''}`;
  c.record({ k: 'Button', id, x, y, w, h, fixed: !!o.w, variant, state: o.state || 'normal', icon: o.icon || '', label: o.label || '', chevron: !!o.chevron,
    badge: o.badge, mark: !!o.mark, busy: !!o.busy, bold: o.weight === 700, iconAccent: o.iconFill === T.accent, px, iconSize: box, size });
  c.add(`<g id="${esc(id)}">`);
  let fo = st.fo, so = st.so, stroke = T.fg, fill = T.fg, textFill = T.fg, textOp;
  if (variant === 'ghost') { fo = o.state === 'normal' || !o.state ? 0 : st.fo; so = o.state === 'normal' || !o.state ? 0 : st.so; }
  if (variant === 'primary') { stroke = T.accent; so = 1; fo = o.state === 'hover' ? 0.18 : 0.08; textFill = T.accent; }
  if (variant === 'danger') { stroke = T.red; so = 0.7; textFill = T.red; }
  if (st.textAccent) textFill = T.accent;
  if (st.textOpacity) textOp = st.textOpacity;
  if (o.state === 'disabled' && variant === 'primary') { stroke = T.fg; so = 0.12; textFill = T.fg; fo = 0; }
  if (o.busy) { textFill = T.accent; }
  if (fo) fillBox(c, x, y, w, h, fo, { fill, id: 'fill' });
  if (so) border(c, x, y, w, h, { stroke, so, id: 'border' });
  // Icon-only buttons centre the box whatever the width (24 and 28 squares: 4 and 6 around it);
  // a primary one stretched wider than it asks (Commit in the stacked bar, Generate now) centres
  // its content as one, the others keep it at the left.
  const slack = variant === 'primary' && o.label ? w - measureButton({ ...o, w: 0 }) : 0;
  let cx = o.icon && !o.label && !o.chevron ? x + (w - box) / 2 : x + px + Math.max(0, Math.floor(slack / 2));
  if (o.icon) { icon(c, o.icon, cx, y + (h - box) / 2, box, { fill: o.iconFill || textFill, opacity: textOp }); cx += box + (o.label ? GAP.icon : 0); }
  if (o.label) { text(c, cx, y + h / 2, o.label, { size, weight: st.bold ? 700 : (o.weight || 400), fill: textFill, opacity: textOp, id: 'label' }); cx += tw(o.label, size); }
  if (o.chevron) icon(c, 'chevron', x + w - px - BOX.chevron, y + (h - BOX.chevron) / 2, BOX.chevron, { fill: textFill, opacity: textOp ?? 0.7 });
  // the badge hangs 4 over the top edge and 4 past the right one
  if (o.badge !== undefined) badge(c, x + w + 4, y - 4, o.badge);
  if (o.mark) { circle(c, x + w - 2, y + 2, 3.5, { fill: T.red, id: 'mark' }); }
  c.add('</g>');
  return w;
}
function chevronIcon(c, x, y, size, fill) { icon(c, 'chevron', x, y, size, { fill, opacity: 0.7 }); }

// A count inside a control: a 16 px pill, 4 either side of the digits.
const pillWidth = n => Math.max(BOX.pill, tw(String(n), 10) + 2 * PAD.pill);
// One segment: [8][icon 16][4][label][4][count pill][8]
const segmentContent = it => (it.icon ? BOX.icon + (it.label ? GAP.icon : 0) : 0) + (it.label ? tw(it.label) : 0) + (it.count !== undefined ? GAP.icon + pillWidth(it.count) : 0);
const measureSegmented = items => items.reduce((a, it) => a + 2 * PAD.control + segmentContent(it), 0);

// Segmented control: items [{label, icon, count, selected}]
function segmented(c, o) {
  const { x, y } = o, h = o.h || BOX.control, px = PAD.control, box = BOX.icon;
  const widths = o.items.map(it => o.stretch ? 0 : 2 * px + segmentContent(it));
  let total = o.w || widths.reduce((a, b) => a + b, 0);
  if (o.stretch) { const each = Math.floor(total / o.items.length); widths.fill(each); widths[widths.length - 1] = total - each * (o.items.length - 1); }
  c.record({ k: 'Segmented', id: o.id || 'Segmented', x, y, w: total, h, stretch: !!o.stretch, items: o.items.map(it => ({ icon: it.icon || '', label: it.label || '', count: it.count, selected: !!it.selected })) });
  c.add(`<g id="${esc(o.id || 'Segmented')}">`);
  fillBox(c, x, y, total, h, 0.04, { id: 'fill' });
  let cx = x;
  o.items.forEach((it, i) => {
    const w = widths[i];
    c.add(`<g id="${esc('Segment/' + (it.selected ? 'selected' : 'normal') + ' ' + (it.label || it.icon))}">`);
    if (it.selected) fillBox(c, cx, y, w, h, 0.18);
    const fill = it.selected ? T.accent : T.fg;
    let ix = o.stretch ? cx + Math.round((w - segmentContent(it)) / 2) : cx + px;
    if (it.icon) { icon(c, it.icon, ix, y + (h - box) / 2, box, { fill }); ix += box + (it.label ? GAP.icon : 0); }
    if (it.label) { text(c, ix, y + h / 2, it.label, { fill, weight: it.selected ? 700 : 400 }); ix += tw(it.label); }
    if (it.count !== undefined) {
      const cw = pillWidth(it.count); ix += GAP.icon;
      rect(c, ix, y + (h - BOX.pill) / 2, cw, BOX.pill, { fill: it.selected ? T.accent : T.fg, fo: it.selected ? 1 : 0.14 });
      text(c, ix + cw / 2, y + h / 2, String(it.count), { size: 10, weight: 700, fill: it.selected ? T.bg : T.fg, anchor: 'middle' });
    }
    c.add('</g>');
    if (i > 0) vline(c, cx, y, h, { fo: 0.4 });
    cx += w;
  });
  border(c, x, y, total, h, { stroke: T.fg, so: 0.4, id: 'border' });
  c.add('</g>');
  return total;
}

// state 'prompt' is the popup search prompt (Omarchy menu look): no box — the
// popup frame is the focus cue — with icon and text on the menu rows' grid.
// [8][icon 16][4][text …][8]: the text starts 28 in, like a menu row's label.
function field(c, o) {
  const { x, y, w } = o, h = o.h || BOX.control, prompt = o.state === 'prompt', st = STATE[prompt ? 'normal' : o.state || 'normal'];
  c.record({ k: 'Field', id: o.id || 'Field/' + (o.state || 'normal'), x, y, w, h, state: o.state || 'normal', icon: o.icon || '', value: o.value || '', placeholder: o.placeholder || '', trailingIcon: o.trailingIcon || '' });
  c.add(`<g id="${esc(o.id || 'Field/' + (o.state || 'normal'))}">`);
  if (!prompt) {
    fillBox(c, x, y, w, h, st.fo, { id: 'fill' });
    border(c, x, y, w, h, { stroke: o.state === 'focus' ? T.accent : T.fg, so: o.state === 'focus' ? 0.6 : st.so, id: 'border' });
  }
  let cx = x + PAD.control;
  if (o.icon) { icon(c, o.icon, cx, y + (h - BOX.icon) / 2, BOX.icon, { fill: T.dim }); cx += BOX.icon + GAP.icon; }
  if (o.value) text(c, cx, y + h / 2, o.value, { id: 'value' });
  else if (o.placeholder) text(c, cx, y + h / 2, o.placeholder, { fill: T.dim, id: 'placeholder' });
  if ((o.state === 'focus' || prompt) && o.value) rect(c, cx + tw(o.value) + 1, y + (h - BOX.line) / 2, 1, BOX.line, { fill: T.fg, id: 'caret' });
  if (o.trailingIcon) icon(c, o.trailingIcon, x + w - PAD.control - BOX.icon, y + (h - BOX.icon) / 2, BOX.icon, { fill: T.dim });
  c.add('</g>');
  return w;
}

// [box 16][8][label]
function checkbox(c, o) {
  const { x, y } = o, s = BOX.check, state = o.checked === 'partial' ? 'partial' : o.checked ? 'checked' : 'off';
  c.record({ k: 'Checkbox', id: o.id || 'Checkbox/' + state + (o.label ? ' ' + o.label : ''), x, y, w: s + (o.label ? GAP.check + tw(o.label) : 0), h: s, state, label: o.label || '', disabled: !!o.disabled });
  c.add(`<g id="${esc(o.id || 'Checkbox/' + state + (o.label ? ' ' + o.label : ''))}">`);
  if (state === 'checked') { rect(c, x, y, s, s, { fill: T.accent }); icon(c, 'check', x, y, s, { fill: T.bg }); }
  else if (state === 'partial') { fillBox(c, x, y, s, s, 0.18); border(c, x, y, s, s, { stroke: T.accent, so: 1 }); icon(c, 'minus', x, y, s, { fill: T.accent }); }
  else { fillBox(c, x, y, s, s, 0.04); border(c, x, y, s, s, { stroke: T.fg, so: o.disabled ? 0.12 : 0.4 }); }
  let w = s;
  if (o.label) { text(c, x + s + GAP.check, y + s / 2, o.label, { fill: T.fg, opacity: o.disabled ? 0.45 : 1 }); w += GAP.check + tw(o.label); }
  c.add('</g>');
  return w;
}

// y is the label's centre line; its box is a 16 px text line.
function sectionLabel(c, x, y, str, o = {}) {
  c.record({ k: 'SectionLabel', id: o.id || 'Section/' + str, x, y: y - BOX.line / 2, w: tw(str, SIZE.caption), h: BOX.line, label: str.toUpperCase() });
  return text(c, x, y, str.toUpperCase(), { size: SIZE.caption, weight: 700, fill: T.dim, ls: 0.8, id: o.id || 'Section/' + str });
}
function dimText(c, x, y, str, o = {}) { return text(c, x, y, str, { size: o.size || SIZE.body, fill: T.dim, anchor: o.anchor, id: o.id }); }

// Chip: reference label used in history rows (branch / remote / tag): 16 high, 4 either side.
function refChip(c, x, y, label, kind = 'local') {
  const fill = kind === 'tag' ? T.yellow : kind === 'remote' ? T.magenta : T.accent;
  const w = tw(label, 10) + 2 * PAD.pill, h = BOX.pill;
  c.record({ k: 'RefChip', id: 'RefChip/' + kind + ' ' + label, x, y, w, h, label, kind });
  c.add(`<g id="${esc('RefChip/' + kind + ' ' + label)}">`);
  if (kind === 'head') rect(c, x, y, w, h, { fill }); else { rect(c, x, y, w, h, { fill, fo: 0.14 }); border(c, x, y, w, h, { stroke: fill, so: 0.8 }); }
  text(c, x + w / 2, y + h / 2, label, { size: 10, weight: 700, fill: kind === 'head' ? T.bg : fill, anchor: 'middle' });
  c.add('</g>');
  return w;
}

// Status colour of a file row
function statusColor(s) {
  return { M: T.blue, A: T.green, D: T.red, R: T.magenta, C: T.red, '?': T.dim, U: T.red }[s] || T.fg;
}
function statusName(s) { return { M: 'Modified', A: 'Added', D: 'Deleted', R: 'Renamed', C: 'Conflict', '?': 'Untracked' }[s]; }

// Small status pill "M", "D" ...
function statusPill(c, x, y, s, o = {}) {
  const col = statusColor(s), w = BOX.pill, h = BOX.pill;
  c.record({ k: 'StatusPill', id: 'StatusPill/' + s, x, y, w, h, status: s });
  c.add(`<g id="${esc('StatusPill/' + s)}">`);
  rect(c, x, y, w, h, { fill: col, fo: 0.18 });
  text(c, x + w / 2, y + h / 2, s, { size: 10, weight: 700, fill: col, anchor: 'middle' });
  c.add('</g>');
  return w;
}

// Scrollbar hint (vertical) with thin handle
function scrollbar(c, x, y, h, o = {}) {
  const hh = Math.max(24, Math.round(h * (o.ratio || 0.35)));
  rect(c, x + 2, y + (o.at || 0), 4, hh, { fill: T.fg, fo: 0.25, id: 'Scrollbar' });
}

module.exports = { THEMES, setTheme, theme, SIZE, FONT, tw, esc, ICON, Canvas, rect, border, fillBox, hairline, vline, text, icon, circle,
  badge, button, measureButton, segmented, measureSegmented, pillWidth, field, checkbox, sectionLabel, dimText, refChip, statusColor, statusName, statusPill, scrollbar, chevronIcon, STATE,
  GRID, SP, BOX, PAD, GAP, DENSITY, BAR, GLYPH };
