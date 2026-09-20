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
};
for (const [k, v] of Object.entries(ICON)) if (!mdi[v]) throw new Error('missing icon ' + k + ' ' + v);

class Canvas {
  constructor(id, w, h) { this.id = id; this.w = w; this.h = h; this.parts = []; this.rec = []; }
  // Manifest of component placements (kind, rect, props) used to swap the flat
  // SVG groups for component instances in Figma.
  record(e) { this.rec.push(e); }
  add(s) { this.parts.push(s); }
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
function icon(c, name, x, y, size = 14, o = {}) {
  const d = mdi[ICON[name]]; if (!d) throw new Error('icon ' + name);
  const k = (size / 24).toFixed(4);
  const op = o.opacity !== undefined ? ` fill-opacity="${o.opacity}"` : '';
  c.add(`<g id="${esc(o.id || 'icon/' + name)}" transform="translate(${x},${y}) scale(${k})"><path d="${d}" fill="${o.fill || T.fg}"${op}/></g>`);
  return size;
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

function badge(c, x, y, n, o = {}) {
  const s = String(n), w = Math.max(14, tw(s, 10) + 8), h = 14;
  rect(c, x - w, y, w, h, { fill: o.fill || T.accent, id: 'badge' });
  text(c, x - w / 2, y + h / 2, s, { size: 10, weight: 700, fill: T.bg, anchor: 'middle' });
  return w;
}

function measureButton(o) {
  const px = o.px ?? 10, gap = 6, is = o.iconSize || 14;
  let w = px * 2;
  if (o.icon) w += is;
  if (o.icon && o.label) w += gap;
  if (o.label) w += tw(o.label, o.size || SIZE.body);
  if (o.chevron) w += 6 + 12;
  return o.w || Math.max(w, o.minW || 0);
}

// variant: default | primary | ghost | danger | icon
function button(c, o) {
  const { x, y } = o, h = o.h || 28, variant = o.variant || 'default', st = STATE[o.state || 'normal'];
  const w = measureButton(o), px = o.px ?? 10, is = o.iconSize || 14, size = o.size || SIZE.body;
  const id = o.id || `Button/${variant}/${o.state || 'normal'}${o.label ? ' ' + o.label : ''}`;
  c.record({ k: 'Button', id, x, y, w, h, fixed: !!o.w, variant, state: o.state || 'normal', icon: o.icon || '', label: o.label || '', chevron: !!o.chevron,
    badge: o.badge, mark: !!o.mark, busy: !!o.busy, bold: o.weight === 700, iconAccent: o.iconFill === T.accent, px, iconSize: is, size });
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
  // Icon-only buttons centre the glyph whatever the width (w: 28 squares).
  let cx = o.icon && !o.label && !o.chevron ? x + (w - is) / 2 : x + px;
  if (o.icon) { icon(c, o.icon, cx, y + (h - is) / 2, is, { fill: o.iconFill || textFill, opacity: textOp }); cx += is + (o.label ? 6 : 0); }
  if (o.label) { text(c, cx, y + h / 2, o.label, { size, weight: st.bold ? 700 : (o.weight || 400), fill: textFill, opacity: textOp, id: 'label' }); cx += tw(o.label, size); }
  if (o.chevron) icon(c, 'chevron', x + w - px - 12 + 2, y + (h - 12) / 2, 12, { fill: textFill, opacity: textOp ?? 0.7 });
  if (o.badge !== undefined) badge(c, x + w + 4, y - 5, o.badge);
  if (o.mark) { circle(c, x + w - 2, y + 2, 3.5, { fill: T.red, id: 'mark' }); }
  c.add('</g>');
  return w;
}
function chevronIcon(c, x, y, size, fill) { icon(c, 'chevron', x, y, size, { fill, opacity: 0.7 }); }

// Segmented control: items [{label, icon, count, selected}]
function segmented(c, o) {
  const { x, y } = o, h = o.h || 28, px = o.px ?? 12, is = 14;
  const widths = o.items.map(it => o.stretch ? 0 : px * 2 + (it.icon ? is + (it.label ? 6 : 0) : 0) + (it.label ? tw(it.label) : 0) + (it.count !== undefined ? 6 + tw(String(it.count), 10) + 8 : 0));
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
    const content = (it.icon ? is + (it.label ? 6 : 0) : 0) + (it.label ? tw(it.label) : 0) + (it.count !== undefined ? 6 + tw(String(it.count), 10) + 8 : 0);
    let ix = o.stretch ? cx + Math.round((w - content) / 2) : cx + px;
    if (it.icon) { icon(c, it.icon, ix, y + (h - is) / 2, is, { fill }); ix += is + (it.label ? 6 : 0); }
    if (it.label) { text(c, ix, y + h / 2, it.label, { fill, weight: it.selected ? 700 : 400 }); ix += tw(it.label); }
    if (it.count !== undefined) {
      const cw = tw(String(it.count), 10) + 8; ix += 6;
      rect(c, ix, y + h / 2 - 7, cw, 14, { fill: it.selected ? T.accent : T.fg, fo: it.selected ? 1 : 0.14 });
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
// popup frame is the focus cue — with icon and text on the menu-row grid.
function field(c, o) {
  const { x, y, w } = o, h = o.h || 28, prompt = o.state === 'prompt', st = STATE[prompt ? 'normal' : o.state || 'normal'];
  c.record({ k: 'Field', id: o.id || 'Field/' + (o.state || 'normal'), x, y, w, h, state: o.state || 'normal', icon: o.icon || '', value: o.value || '', placeholder: o.placeholder || '', trailingIcon: o.trailingIcon || '' });
  c.add(`<g id="${esc(o.id || 'Field/' + (o.state || 'normal'))}">`);
  if (!prompt) {
    fillBox(c, x, y, w, h, st.fo, { id: 'fill' });
    border(c, x, y, w, h, { stroke: o.state === 'focus' ? T.accent : T.fg, so: o.state === 'focus' ? 0.6 : st.so, id: 'border' });
  }
  let cx = x + (prompt ? 10 : 8);
  if (o.icon) { icon(c, o.icon, cx, y + (h - 14) / 2, 14, { fill: T.dim }); cx += prompt ? 22 : 20; }
  if (o.value) text(c, cx, y + h / 2, o.value, { id: 'value' });
  else if (o.placeholder) text(c, cx, y + h / 2, o.placeholder, { fill: T.dim, id: 'placeholder' });
  if ((o.state === 'focus' || prompt) && o.value) rect(c, cx + tw(o.value) + 1, y + 7, 1, h - 14, { fill: T.fg, id: 'caret' });
  if (o.trailingIcon) icon(c, x + w - 22, y + (h - 14) / 2, o.trailingIcon, 14, { fill: T.dim });
  c.add('</g>');
  return w;
}

function checkbox(c, o) {
  const { x, y } = o, s = 14, state = o.checked === 'partial' ? 'partial' : o.checked ? 'checked' : 'off';
  c.record({ k: 'Checkbox', id: o.id || 'Checkbox/' + state + (o.label ? ' ' + o.label : ''), x, y, w: s + (o.label ? 8 + tw(o.label) : 0), h: s, state, label: o.label || '', disabled: !!o.disabled });
  c.add(`<g id="${esc(o.id || 'Checkbox/' + state + (o.label ? ' ' + o.label : ''))}">`);
  if (state === 'checked') { rect(c, x, y, s, s, { fill: T.accent }); icon(c, 'check', x + 1, y + 1, 12, { fill: T.bg }); }
  else if (state === 'partial') { fillBox(c, x, y, s, s, 0.18); border(c, x, y, s, s, { stroke: T.accent, so: 1 }); icon(c, 'minus', x + 1, y + 1, 12, { fill: T.accent }); }
  else { fillBox(c, x, y, s, s, 0.04); border(c, x, y, s, s, { stroke: T.fg, so: o.disabled ? 0.12 : 0.4 }); }
  let w = s;
  if (o.label) { text(c, x + s + 8, y + s / 2, o.label, { fill: T.fg, opacity: o.disabled ? 0.45 : 1 }); w += 8 + tw(o.label); }
  c.add('</g>');
  return w;
}

function sectionLabel(c, x, y, str, o = {}) {
  c.record({ k: 'SectionLabel', id: o.id || 'Section/' + str, x, y: y - 6, w: tw(str, SIZE.caption), h: 12, label: str.toUpperCase() });
  return text(c, x, y, str.toUpperCase(), { size: SIZE.caption, weight: 700, fill: T.dim, ls: 0.8, id: o.id || 'Section/' + str });
}
function dimText(c, x, y, str, o = {}) { return text(c, x, y, str, { size: o.size || SIZE.body, fill: T.dim, anchor: o.anchor, id: o.id }); }

// Chip: reference label used in history rows (branch / remote / tag)
function refChip(c, x, y, label, kind = 'local') {
  const fill = kind === 'tag' ? T.yellow : kind === 'remote' ? T.magenta : T.accent;
  const w = tw(label, 10) + 10, h = 16;
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
  const col = statusColor(s), w = 16, h = 16;
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
  badge, button, measureButton, segmented, field, checkbox, sectionLabel, dimText, refChip, statusColor, statusName, statusPill, scrollbar, chevronIcon, STATE };
