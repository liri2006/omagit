// Adds the `IconButton` component set (variant × state × size, Icon swap) to
// the Components page and swaps every icon-only Button instance in the file for
// it, re-placed at the rects of out/manifest.json (which realigned the inline
// icon buttons to 24 px, flush with the content edge). Run after lib.js with
// manifest.json uploaded. Re-runnable: an existing IconButton set is replaced.
(async () => {
  const om = window.__om; om.log = []; await om.init();
  const M = JSON.parse(await om.readFile('manifest.json'));
  const cpage = om.page('Components'); figma.currentPage = cpage;
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const top = cframe.findAll(n => (n.type === 'COMPONENT_SET' || n.type === 'COMPONENT') && n.parent.type !== 'COMPONENT_SET');
  const comp = {}; top.forEach(n => { comp[n.name] = n; });
  const oldSet = comp.IconButton; if (oldSet) oldSet.name = 'IconButton (old)';
  const iconComps = {}; comp.Icon.children.forEach(c => { iconComps[c.name.slice(5)] = c; });
  const glyph = ic => om.child(ic, 'glyph');

  // ---- component set: variant=ghost|default × state × size=24|28
  const STATE = { normal: { fo: .04, so: .40 }, hover: { fo: .08, so: .25 }, pressed: { fo: .22, so: .25 }, selected: { fo: .18, so: .18, accent: true }, disabled: { fo: 0, so: .12, op: .45 } };
  const mkIconButton = (variant, state, size) => {
    const st = STATE[state]; let fo = st.fo, so = st.so;
    if (variant === 'ghost' && state === 'normal') { fo = 0; so = 0; }
    const f = figma.createFrame(); f.name = `variant=${variant}, state=${state}, size=${size}`; f.fills = []; f.strokes = []; f.clipsContent = false;
    f.layoutMode = 'HORIZONTAL'; f.primaryAxisSizingMode = 'FIXED'; f.counterAxisSizingMode = 'FIXED'; f.resizeWithoutConstraints(size, size);
    f.paddingLeft = f.paddingRight = f.paddingTop = f.paddingBottom = 0; f.primaryAxisAlignItems = 'CENTER'; f.counterAxisAlignItems = 'CENTER';
    om.chrome(f, { fo, so });
    const ic = iconComps.refresh.createInstance(); ic.name = 'Icon'; f.appendChild(ic); om.fill(glyph(ic), st.accent ? 'Accent' : 'Foreground'); ic.opacity = st.op || 1;
    om.fitChrome(f);
    return figma.createComponentFromNode(f);
  };
  const VARIANTS = ['ghost', 'default'], STATES = ['normal', 'hover', 'pressed', 'selected', 'disabled'], SIZES = [24, 28];
  const nodes = []; for (const v of VARIANTS) for (const sz of SIZES) for (const s of STATES) nodes.push(mkIconButton(v, s, sz));
  const set = figma.combineAsVariants(nodes, cframe); set.name = 'IconButton';
  const iconProp = set.addComponentProperty('Icon', 'INSTANCE_SWAP', iconComps.refresh.id);
  set.children.forEach(c => { om.child(c, 'Icon').componentPropertyReferences = { mainComponent: iconProp }; });
  // grid: one row per variant × size, states as columns
  set.children.forEach((c, i) => { c.x = 16 + (i % STATES.length) * 44; c.y = 16 + Math.floor(i / STATES.length) * 44; });
  set.resizeWithoutConstraints(16 + STATES.length * 44, 16 + VARIANTS.length * SIZES.length * 44);
  // prototype: hovering / pressing a normal variant shows the hover / pressed one
  const variantOf = (v, s, sz) => set.children.find(c => c.name === `variant=${v}, state=${s}, size=${sz}`);
  let wired = 0;
  for (const v of VARIANTS) for (const sz of SIZES) {
    const n = variantOf(v, 'normal', sz);
    const rx = [['ON_HOVER', variantOf(v, 'hover', sz)], ['ON_PRESS', variantOf(v, 'pressed', sz)]].map(([t, d]) => ({ trigger: { type: t }, actions: [{ type: 'NODE', destinationId: d.id, navigation: 'CHANGE_TO', transition: null, preserveScrollPosition: false }] }));
    try { if (n.setReactionsAsync) await n.setReactionsAsync(rx); else n.reactions = rx; wired++; } catch (e) { om.say('reactions failed', e.message); }
  }
  om.say('IconButton variants', nodes.length, 'hover/press reactions on', wired);
  // place it on the Button row, right of the last node there
  const row = ['Button', 'Chip/branch', 'Chip/repo', 'Badge', 'SyncDropdown'].map(n => comp[n]).filter(Boolean);
  const rowY = comp.Button.y, rowRight = Math.max(...row.map(n => n.x + n.width));
  cframe.appendChild(set); set.x = rowRight + 40; set.y = rowY;
  if (set.x + set.width > cframe.width - 40) om.say('note: IconButton set overflows the Components frame; right edge', Math.round(set.x + set.width), 'frame width', cframe.width);
  const cap = cframe.findOne(n => n.type === 'TEXT' && /^caption Button/.test(n.name));
  if (cap) cap.characters = cap.characters.replace(/ · SyncDropdown.*$/, ' · SyncDropdown · IconButton (variant ghost / default, state, size 24 / 28, Icon)');

  // ---- swap instances
  const setOf = inst => { const mc = inst.mainComponent; return mc && mc.parent && mc.parent.type === 'COMPONENT_SET' ? mc.parent : null; };
  const propVal = (inst, name) => { const k = om.propKey(inst, name); return k ? inst.componentProperties[k].value : undefined; };
  const isIconOnly = inst => { const s = setOf(inst); if (!s) return false; if (s.name === 'IconButton (old)') return true; if (s.name !== 'Button') return false; return propVal(inst, 'Show icon') === true && propVal(inst, 'Show label') === false; };
  const themeOf = inst => { const g = glyph(om.child(inst, 'Icon')); const nm = g && om.styleById[g.fillStyleId]; return nm && nm.startsWith('Light/') ? 'Light' : 'Dark'; };
  const swap = (old, r) => {
    const variant = r ? r.variant : (propVal(old, 'variant') || 'ghost'), state = r ? r.state : (propVal(old, 'state') || 'normal');
    const v = variant === 'default' ? 'default' : 'ghost';
    const w = r ? r.w : old.width, h = r ? r.h : old.height, size = h === 24 ? '24' : '28';
    const iconId = r ? (iconComps[r.icon] && iconComps[r.icon].id) : propVal(old, 'Icon');
    const theme = themeOf(old);
    const inst = set.defaultVariant.createInstance();
    om.setProps(inst, { variant: v, state, size, Icon: iconId });
    inst.name = r ? r.id : (/^variant=/.test(old.name) ? 'IconButton' : old.name);
    if (w !== +size || h !== +size) inst.resize(w, h);
    if (theme === 'Light') om.retheme(inst, 'Light');
    const parent = old.parent, idx = parent.children.indexOf(old); parent.insertChild(idx, inst);
    const ob = old.absoluteBoundingBox; const fb = r ? r.frameBox : null;
    const tx = fb ? fb.x + r.x : ob.x + (ob.width - w) / 2, ty = fb ? fb.y + r.y : ob.y + (ob.height - h) / 2;
    const at = inst.absoluteTransform; inst.x += tx - at[0][2]; inst.y += ty - at[1][2];
    old.remove();
    return inst;
  };
  const stats = { byManifest: 0, inPlace: 0, missing: [] };
  const done = new Set();
  for (const [fname, { rec }] of Object.entries(M)) {
    const frame = om.frame(fname); if (!frame) { om.say('no frame', fname); continue; }
    figma.currentPage = frame.parent;
    const fb = frame.absoluteBoundingBox;
    const dy = fname === 'Components' ? (Math.min(...cframe.findAll(n => /^caption (Tables|Diff|Mini)/.test(n.name)).map(n => n.y)) - 356) : 0;
    const insts = frame.findAll(n => n.type === 'INSTANCE' && isIconOnly(n));
    for (const r0 of rec) {
      if (!(r0.k === 'Button' && r0.icon && !r0.label && r0.fixed)) continue;
      if (fname === 'Components' && r0.y < 350) continue;
      const r = { ...r0, y: r0.y + dy, frameBox: fb };
      const cx = fb.x + r.x + r.w / 2, cy = fb.y + r.y + r.h / 2;
      const want = iconComps[r.icon] && iconComps[r.icon].id;
      const cands = insts.filter(c => !c.removed && !done.has(c.id) && propVal(c, 'Icon') === want).map(c => { const b = c.absoluteBoundingBox; const bx = b.x + b.width / 2, by = b.y + b.height / 2; return { c, d: Math.hypot(bx - cx, by - cy) }; }).filter(x => x.d <= 12).sort((a, b) => a.d - b.d);
      if (!cands.length) { stats.missing.push(fname + ':' + r.id + '@' + r.x + ',' + r.y); continue; }
      done.add(cands[0].c.id); swap(cands[0].c, r); stats.byManifest++;
    }
  }
  // anything icon-only that the manifest did not cover (sheets, overlays): swap in place
  for (const page of figma.root.children) {
    const rest = page.findAll(n => n.type === 'INSTANCE' && !n.removed && isIconOnly(n) && !done.has(n.id));
    for (const n of rest) { try { if (n.parent && n.parent.type !== 'COMPONENT_SET') { swap(n, null); stats.inPlace++; } } catch (e) { om.say('in-place swap failed', page.name, n.name, e.message); } }
  }
  if (oldSet) { const left = oldSet.children.reduce((a, c) => a + c.instances.length, 0); if (left) om.say('old IconButton kept, instances left', left); else oldSet.remove(); }
  om.say('swapped via manifest', stats.byManifest, 'in place', stats.inPlace, 'missing', stats.missing.length, stats.missing.slice(0, 30).join(' | '));
  figma.currentPage = cpage; figma.viewport.scrollAndZoomIntoView([set]);
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
