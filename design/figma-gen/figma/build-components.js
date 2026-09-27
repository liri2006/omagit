// Rebuilds the control components of the Components page as auto-layout
// components with properties (Label, Icon swap, Show …). Run after lib.js with
// icons.json uploaded to the hidden file input. Re-runnable: the previous
// generation is removed and the pattern rows are re-laid out.
(async () => {
  const om = window.__om; om.log = []; await om.init();
  if (window.__scratch) { window.__scratch.forEach(n => { try { n.remove(); } catch (e) {} }); window.__scratch = null; }
  const ICONS = JSON.parse(await om.readFile('icons.json'));
  const cpage = om.page('Components'); figma.currentPage = cpage;
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const OLD = ['Button/default', 'Button/primary', 'Button/ghost', 'Button/danger', 'Button/icon', 'Button/badge', 'Button/busy', 'Button/mark', 'Button/dropdown',
    'Chip/branch', 'Chip/repo', 'SyncDropdown', 'SyncDropdown/mini', 'SectionLabel', 'Segmented', 'Field', 'Checkbox', 'RefChip', 'StatusPill', 'Badge', 'Icon', 'Segment', 'Button', 'IconButton'];
  const isTop = n => (n.type === 'COMPONENT_SET' || n.type === 'COMPONENT') && n.parent.type !== 'COMPONENT_SET';
  const old = cframe.findAll(n => isTop(n) && OLD.includes(n.name));
  om.say('old components', old.length);

  const mkFrame = (name, w, h) => { const f = figma.createFrame(); f.name = name; f.resizeWithoutConstraints(w, h); f.fills = []; f.strokes = []; f.clipsContent = false; f.cornerRadius = 0; return f; };
  const hbox = (name, { h = 28, px = 8, gap = 4 } = {}) => {
    const f = mkFrame(name, 10, h); f.layoutMode = 'HORIZONTAL'; f.primaryAxisSizingMode = 'AUTO'; f.counterAxisSizingMode = 'FIXED';
    f.paddingLeft = f.paddingRight = px; f.paddingTop = f.paddingBottom = 0; f.itemSpacing = gap; f.counterAxisAlignItems = 'CENTER'; f.primaryAxisAlignItems = 'CENTER'; return f;
  };
  const absolute = (n, hc, vc) => { n.layoutPositioning = 'ABSOLUTE'; n.constraints = { horizontal: hc, vertical: vc }; };
  const setLayout = (set, o = {}) => { try { set.layoutMode = 'HORIZONTAL'; set.itemSpacing = 16; set.counterAxisSpacing = 16; set.paddingLeft = set.paddingRight = set.paddingTop = set.paddingBottom = 16; set.counterAxisAlignItems = 'CENTER'; if (o.wrap) { set.layoutWrap = 'WRAP'; set.primaryAxisSizingMode = 'FIXED'; set.counterAxisSizingMode = 'AUTO'; set.resize(o.wrap, set.height); } else { set.primaryAxisSizingMode = 'AUTO'; set.counterAxisSizingMode = 'AUTO'; } } catch (e) { om.say('set layout failed', set.name, e.message); } };
  const glyph = ic => om.child(ic, 'glyph');
  const done = f => { om.fitChrome(f); return figma.createComponentFromNode(f); };

  // ---- Icon set: 16 px boxes, the glyph drawn at 14 in the middle (kit.js icon(): 7/8 of the box)
  const iconComps = {}; const iconNodes = [];
  for (const [name, d] of Object.entries(ICONS)) {
    const f = figma.createNodeFromSvg(om.iconSvg(d));
    f.name = 'name=' + name; f.fills = []; f.clipsContent = false;
    f.findAll(n => n.type === 'VECTOR' || n.type === 'BOOLEAN_OPERATION').forEach(v => { v.name = 'glyph'; om.fill(v, 'Foreground'); v.constraints = { horizontal: 'SCALE', vertical: 'SCALE' }; });
    const c = figma.createComponentFromNode(f); iconComps[name] = c; iconNodes.push(c);
  }
  const iconSet = figma.combineAsVariants(iconNodes, cframe); iconSet.name = 'Icon'; setLayout(iconSet, { wrap: 420 });
  const icon = (name, size = 16, nodeName = 'Icon') => { const i = iconComps[name].createInstance(); i.name = nodeName; if (size !== 16) i.resize(size, size); return i; };
  om.say('icons', iconNodes.length);

  // ---- Badge: 12 high, hanging 4 over a button's top-right corner
  const badge = hbox('Badge', { h: 12, px: 2, gap: 0 }); try { badge.minWidth = 12; } catch (e) {} om.fill(badge, 'Accent');
  badge.appendChild(om.textNode('2', 'Caption 10/Bold', 'Background', 1, 'Count'));
  const badgeComp = figma.createComponentFromNode(badge); cframe.appendChild(badgeComp);
  om.child(badgeComp, 'Count').componentPropertyReferences = { characters: badgeComp.addComponentProperty('Count', 'TEXT', '2') };

  // ---- Button
  const STATE = { normal: { fo: .04, so: .40 }, hover: { fo: .08, so: .25 }, pressed: { fo: .22, so: .25 }, selected: { fo: .18, so: .18, accent: true, bold: true }, disabled: { fo: 0, so: .12, textOp: .45 } };
  const chrome = (variant, state) => {
    const st = STATE[state]; let fo = st.fo, so = st.so, strokeRole = 'Foreground', textRole = 'Foreground', textOp = 1, bold = !!st.bold;
    if (variant === 'ghost' && state === 'normal') { fo = 0; so = 0; }
    if (variant === 'primary') { strokeRole = 'Accent'; so = 1; fo = state === 'hover' ? .18 : .08; textRole = 'Accent'; }
    if (variant === 'danger') { strokeRole = 'Red'; so = .7; textRole = 'Red'; }
    if (st.accent) textRole = 'Accent';
    if (st.textOp) textOp = st.textOp;
    if (state === 'disabled' && variant === 'primary') { strokeRole = 'Foreground'; so = .12; textRole = 'Foreground'; fo = 0; }
    return { fo, so, strokeRole, textRole, textOp, bold };
  };
  const mkButton = (name, variant, state, o = {}) => {
    const ch = chrome(variant, state);
    const f = hbox(name, { h: 28, px: o.px || 8, gap: 4 });
    om.chrome(f, { fo: ch.fo, strokeRole: ch.strokeRole, so: ch.so });
    const ic = icon(o.icon || 'pull'); f.appendChild(ic); om.fill(glyph(ic), o.iconRole || ch.textRole); ic.opacity = ch.textOp;
    const t = om.textNode(o.label || 'Label', 'Body 12/' + (ch.bold || o.bold ? 'Bold' : 'Regular'), o.labelRole || ch.textRole); t.opacity = ch.textOp; f.appendChild(t);
    const cv = icon('chevron', 12, 'Chevron'); f.appendChild(cv); om.fill(glyph(cv), ch.textRole); cv.opacity = 0.7 * ch.textOp; cv.visible = !!o.chevron;
    const b = badgeComp.createInstance(); b.name = 'Badge'; f.appendChild(b); absolute(b, 'MAX', 'MIN'); b.x = f.width + 4 - b.width; b.y = -4; b.visible = false;
    const m = figma.createEllipse(); m.name = 'Mark'; m.resize(7, 7); om.fill(m, 'Red'); f.appendChild(m); absolute(m, 'MAX', 'MIN'); m.x = f.width - 5.5; m.y = -1.5; m.visible = false;
    return f;
  };
  const wireButton = (c, p) => {
    om.child(c, 'Label').componentPropertyReferences = { characters: p.label, visible: p.showLabel };
    om.child(c, 'Icon').componentPropertyReferences = { mainComponent: p.icon, visible: p.showIcon };
    om.child(c, 'Chevron').componentPropertyReferences = { visible: p.chevron };
    if (p.badge) om.child(c, 'Badge').componentPropertyReferences = { visible: p.badge };
    if (p.mark) om.child(c, 'Mark').componentPropertyReferences = { visible: p.mark };
  };
  const buttonProps = (host, withBadge) => ({
    label: host.addComponentProperty('Label', 'TEXT', 'Label'), icon: host.addComponentProperty('Icon', 'INSTANCE_SWAP', iconComps.pull.id),
    showIcon: host.addComponentProperty('Show icon', 'BOOLEAN', true), showLabel: host.addComponentProperty('Show label', 'BOOLEAN', true),
    chevron: host.addComponentProperty('Show chevron', 'BOOLEAN', false),
    badge: withBadge ? host.addComponentProperty('Show badge', 'BOOLEAN', false) : null, mark: withBadge ? host.addComponentProperty('Show mark', 'BOOLEAN', false) : null,
  });
  const VARIANTS = ['default', 'primary', 'ghost', 'danger'], STATES = ['normal', 'hover', 'pressed', 'selected', 'disabled'];
  const sample = { default: ['pull', 'Pull'], primary: ['commit', 'Commit'], ghost: ['sparkle', 'Generate'], danger: ['close', 'Abort merge'] };
  const bnodes = [];
  for (const v of VARIANTS) for (const s of STATES) bnodes.push(done(mkButton(`variant=${v}, state=${s}`, v, s, { icon: sample[v][0], label: sample[v][1], px: v === 'primary' ? 16 : 8 })));
  const buttonSet = figma.combineAsVariants(bnodes, cframe); buttonSet.name = 'Button';
  const bp = buttonProps(buttonSet, true); buttonSet.children.forEach(c => wireButton(c, bp));
  const colW = STATES.map((s, si) => Math.max(...buttonSet.children.filter((c, i) => i % STATES.length === si).map(c => c.width)));
  let gx = 16; const colX = colW.map(w => { const x = gx; gx += w + 16; return x; });
  buttonSet.children.forEach((c, i) => { c.x = colX[i % STATES.length]; c.y = 16 + Math.floor(i / STATES.length) * 44; });
  buttonSet.resizeWithoutConstraints(gx, 16 + VARIANTS.length * 44);
  const chipBranch = done(mkButton('Chip/branch', 'ghost', 'normal', { icon: 'branch', label: 'main', chevron: true, bold: true, iconRole: 'Accent', labelRole: 'Accent' }));
  const chipRepo = done(mkButton('Chip/repo', 'ghost', 'normal', { icon: 'folderOpen', label: 'omagit', chevron: true }));
  for (const c of [chipBranch, chipRepo]) { cframe.appendChild(c); wireButton(c, buttonProps(c, false)); }

  // ---- IconButton: square, icon centred; variant × state × size, Icon swap (see figma/icon-buttons.js)
  const mkIconButton = (variant, state, size) => {
    const ch = chrome(variant, state);
    const f = hbox(`variant=${variant}, state=${state}, size=${size}`, { h: size, px: 0, gap: 0 }); f.primaryAxisSizingMode = 'FIXED'; f.resizeWithoutConstraints(size, size);
    om.chrome(f, { fo: ch.fo, so: ch.so });
    const ic = icon('refresh'); f.appendChild(ic); om.fill(glyph(ic), ch.textRole); ic.opacity = ch.textOp;
    return done(f);
  };
  const IB_VARIANTS = ['ghost', 'default'], IB_SIZES = [24, 28];
  const ibNodes = []; for (const v of IB_VARIANTS) for (const sz of IB_SIZES) for (const s of STATES) ibNodes.push(mkIconButton(v, s, sz));
  const iconButtonSet = figma.combineAsVariants(ibNodes, cframe); iconButtonSet.name = 'IconButton';
  const ibIcon = iconButtonSet.addComponentProperty('Icon', 'INSTANCE_SWAP', iconComps.refresh.id);
  iconButtonSet.children.forEach((c, i) => { om.child(c, 'Icon').componentPropertyReferences = { mainComponent: ibIcon }; c.x = 16 + (i % STATES.length) * 44; c.y = 16 + Math.floor(i / STATES.length) * 44; });
  iconButtonSet.resizeWithoutConstraints(16 + STATES.length * 44, 16 + IB_VARIANTS.length * IB_SIZES.length * 44);
  for (const v of IB_VARIANTS) for (const sz of IB_SIZES) {
    const at = st => iconButtonSet.children.find(c => c.name === `variant=${v}, state=${st}, size=${sz}`);
    const rx = [['ON_HOVER', at('hover')], ['ON_PRESS', at('pressed')]].map(([t, d]) => ({ trigger: { type: t }, actions: [{ type: 'NODE', destinationId: d.id, navigation: 'CHANGE_TO', transition: null, preserveScrollPosition: false }] }));
    try { const n = at('normal'); if (n.setReactionsAsync) await n.setReactionsAsync(rx); else n.reactions = rx; } catch (e) { om.say('reactions failed', e.message); }
  }

  // ---- Segment / Segmented
  const mkSegment = (name, selected) => {
    const f = hbox(name, { h: 28, px: 8, gap: 4 }); om.chrome(f, { fo: selected ? .18 : 0, so: 0 });
    const role = selected ? 'Accent' : 'Foreground';
    const dv = figma.createRectangle(); dv.name = 'Divider'; dv.resize(1, 28); om.fill(dv, 'Foreground'); dv.opacity = .4; f.appendChild(dv); absolute(dv, 'MIN', 'STRETCH'); dv.x = 0; dv.y = 0;
    const ic = icon('commit'); f.appendChild(ic); om.fill(glyph(ic), role);
    f.appendChild(om.textNode('Changes', 'Body 12/' + (selected ? 'Bold' : 'Regular'), role));
    const cnt = hbox('Count', { h: 16, px: 4, gap: 0 }); try { cnt.minWidth = 16; } catch (e) {}
    if (selected) om.fill(cnt, 'Accent'); else om.chrome(cnt, { fo: .14, so: 0 });
    cnt.appendChild(om.textNode('7', 'Caption 10/Bold', selected ? 'Background' : 'Foreground', 1, 'CountText')); om.fitChrome(cnt); f.appendChild(cnt);
    return f;
  };
  const segmentSet = figma.combineAsVariants([done(mkSegment('state=normal', false)), done(mkSegment('state=selected', true))], cframe);
  segmentSet.name = 'Segment'; setLayout(segmentSet);
  const sp = { label: segmentSet.addComponentProperty('Label', 'TEXT', 'Changes'), count: segmentSet.addComponentProperty('Count', 'TEXT', '7'), icon: segmentSet.addComponentProperty('Icon', 'INSTANCE_SWAP', iconComps.commit.id),
    showIcon: segmentSet.addComponentProperty('Show icon', 'BOOLEAN', true), showLabel: segmentSet.addComponentProperty('Show label', 'BOOLEAN', true), showCount: segmentSet.addComponentProperty('Show count', 'BOOLEAN', true), divider: segmentSet.addComponentProperty('Show divider', 'BOOLEAN', true) };
  segmentSet.children.forEach(c => { om.child(c, 'Label').componentPropertyReferences = { characters: sp.label, visible: sp.showLabel }; om.child(c, 'CountText').componentPropertyReferences = { characters: sp.count }; om.child(c, 'Count').componentPropertyReferences = { visible: sp.showCount }; om.child(c, 'Icon').componentPropertyReferences = { mainComponent: sp.icon, visible: sp.showIcon }; om.child(c, 'Divider').componentPropertyReferences = { visible: sp.divider }; });
  const mkSegmented = (name, stretch) => {
    const f = hbox(name, { h: 28, px: 0, gap: 0 }); om.chrome(f, { fo: .04, so: .4 });
    [['commit', 'Changes', '7'], ['diff', 'Diff', ''], ['history', 'History', '']].forEach(([ic, lb, ct], i) => {
      const s = segmentSet.defaultVariant.createInstance(); s.name = 'Segment ' + (i + 1); f.appendChild(s);
      om.setProps(s, { state: i === 0 ? 'selected' : 'normal', Label: lb, Count: ct || '0', Icon: iconComps[ic].id, 'Show count': !!ct, 'Show divider': i > 0 });
      if (stretch) s.layoutGrow = 1;
    });
    if (stretch) { f.primaryAxisSizingMode = 'FIXED'; f.resize(360, 28); }
    return f;
  };
  const segmentedSet = figma.combineAsVariants([done(mkSegmented('mode=hug', false)), done(mkSegmented('mode=stretch', true))], cframe);
  segmentedSet.name = 'Segmented'; setLayout(segmentedSet);
  const seg3 = segmentedSet.addComponentProperty('Show segment 3', 'BOOLEAN', true);
  segmentedSet.children.forEach(c => { om.child(c, 'Segment 3').componentPropertyReferences = { visible: seg3 }; });

  // ---- Field
  const mkField = (name, state) => {
    const f = hbox(name, { h: 28, px: 8, gap: 4 }); f.primaryAxisSizingMode = 'FIXED'; f.primaryAxisAlignItems = 'MIN'; f.resize(110, 28); // narrow so the three variants fit the 1500 px sheet; instances are resized anyway
    // prompt: the popup search prompt — no chrome, content on the menu rows' grid ([8][icon 16][4][text])
    if (state !== 'prompt') om.chrome(f, state === 'focus' ? { fo: .08, strokeRole: 'Accent', so: .6 } : { fo: .04, so: .4 });
    const ic = icon('search'); f.appendChild(ic); om.fill(glyph(ic), 'Muted'); ic.visible = false;
    const v = om.textNode('Value', 'Body 12/Regular', 'Foreground', 1, 'Value'); f.appendChild(v); v.visible = false;
    f.appendChild(om.textNode('Placeholder', 'Body 12/Regular', 'Muted', 1, 'Placeholder'));
    const caret = figma.createRectangle(); caret.name = 'Caret'; caret.resize(1, 16); om.fill(caret, 'Foreground'); f.appendChild(caret); caret.visible = state === 'focus';
    const tr = icon('chevron', 16, 'Trailing icon'); f.appendChild(tr); absolute(tr, 'MAX', 'CENTER'); tr.x = 110 - 8 - 16; tr.y = 6; om.fill(glyph(tr), 'Muted'); tr.visible = false;
    return f;
  };
  const fieldSet = figma.combineAsVariants([done(mkField('state=normal', 'normal')), done(mkField('state=focus', 'focus')), done(mkField('state=prompt', 'prompt'))], cframe);
  fieldSet.name = 'Field'; setLayout(fieldSet);
  const fp = { value: fieldSet.addComponentProperty('Value', 'TEXT', 'Value'), ph: fieldSet.addComponentProperty('Placeholder', 'TEXT', 'Placeholder'), icon: fieldSet.addComponentProperty('Icon', 'INSTANCE_SWAP', iconComps.search.id),
    showIcon: fieldSet.addComponentProperty('Show icon', 'BOOLEAN', false), showValue: fieldSet.addComponentProperty('Show value', 'BOOLEAN', false), showPh: fieldSet.addComponentProperty('Show placeholder', 'BOOLEAN', true),
    caret: fieldSet.addComponentProperty('Show caret', 'BOOLEAN', false), tr: fieldSet.addComponentProperty('Trailing icon', 'INSTANCE_SWAP', iconComps.chevron.id), showTr: fieldSet.addComponentProperty('Show trailing icon', 'BOOLEAN', false) };
  fieldSet.children.forEach(c => { om.child(c, 'Value').componentPropertyReferences = { characters: fp.value, visible: fp.showValue }; om.child(c, 'Placeholder').componentPropertyReferences = { characters: fp.ph, visible: fp.showPh }; om.child(c, 'Icon').componentPropertyReferences = { mainComponent: fp.icon, visible: fp.showIcon }; om.child(c, 'Caret').componentPropertyReferences = { visible: fp.caret }; om.child(c, 'Trailing icon').componentPropertyReferences = { mainComponent: fp.tr, visible: fp.showTr }; });

  // ---- Checkbox
  const mkCheckbox = (name, state) => {
    const f = hbox(name, { h: 16, px: 0, gap: 8 }); f.primaryAxisAlignItems = 'MIN';
    const box = mkFrame('Box', 16, 16); box.layoutMode = 'HORIZONTAL'; box.primaryAxisSizingMode = 'FIXED'; box.counterAxisSizingMode = 'FIXED'; box.resize(16, 16); f.appendChild(box);
    if (state === 'checked') { om.fill(box, 'Accent'); const ic = icon('check'); box.appendChild(ic); absolute(ic, 'MIN', 'MIN'); ic.x = 0; ic.y = 0; om.fill(glyph(ic), 'Background'); }
    else if (state === 'partial') { om.chrome(box, { fo: .18, strokeRole: 'Accent', so: 1 }); const ic = icon('minus'); box.appendChild(ic); absolute(ic, 'MIN', 'MIN'); ic.x = 0; ic.y = 0; om.fill(glyph(ic), 'Accent'); }
    else om.chrome(box, { fo: .04, so: state === 'disabled' ? .12 : .4 });
    om.fitChrome(box);
    const t = om.textNode('Label', 'Body 12/Regular', 'Foreground'); t.opacity = state === 'disabled' ? .45 : 1; f.appendChild(t);
    return f;
  };
  const checkboxSet = figma.combineAsVariants(['off', 'checked', 'partial', 'disabled'].map(s => done(mkCheckbox('state=' + s, s))), cframe);
  checkboxSet.name = 'Checkbox'; setLayout(checkboxSet);
  const cp = { label: checkboxSet.addComponentProperty('Label', 'TEXT', 'Label'), show: checkboxSet.addComponentProperty('Show label', 'BOOLEAN', true) };
  checkboxSet.children.forEach(c => { om.child(c, 'Label').componentPropertyReferences = { characters: cp.label, visible: cp.show }; });

  // ---- RefChip
  const mkRefChip = (kind) => {
    const role = kind === 'tag' ? 'Yellow' : kind === 'remote' ? 'Magenta' : 'Accent';
    const f = hbox('kind=' + kind, { h: 16, px: 4, gap: 0 });
    if (kind === 'head') om.fill(f, role); else om.chrome(f, { fillRole: role, fo: .14, strokeRole: role, so: .8 });
    f.appendChild(om.textNode({ head: 'main', local: 'feature/askpass', remote: 'origin/main', tag: 'v0.4' }[kind], 'Caption 10/Bold', kind === 'head' ? 'Background' : role));
    return f;
  };
  const refSet = figma.combineAsVariants(['head', 'local', 'remote', 'tag'].map(k => done(mkRefChip(k))), cframe); refSet.name = 'RefChip'; setLayout(refSet);
  const rl = refSet.addComponentProperty('Label', 'TEXT', 'main'); refSet.children.forEach(c => { om.child(c, 'Label').componentPropertyReferences = { characters: rl }; });

  // ---- StatusPill
  const PILL = { M: 'Blue', A: 'Green', D: 'Red', R: 'Magenta', C: 'Red', '?': 'Muted' };
  const pillSet = figma.combineAsVariants(Object.keys(PILL).map(s => { const f = hbox('status=' + s, { h: 16, px: 0, gap: 0 }); f.primaryAxisSizingMode = 'FIXED'; f.resize(16, 16); om.chrome(f, { fillRole: PILL[s], fo: .18, so: 0 }); f.appendChild(om.textNode(s, 'Caption 10/Bold', PILL[s])); return done(f); }), cframe);
  pillSet.name = 'StatusPill'; setLayout(pillSet);

  // ---- SyncDropdown: borderless, no chevron, [4][↓ 16][2][8][↑ 16][1][6], 66 wide (screens.js syncDropdown)
  const sd = mkFrame('SyncDropdown', 66, 28);
  const put = (n, x, y) => { sd.appendChild(n); n.x = x; n.y = y; };
  put(icon('down', 16, 'Icon down'), 4, 6);
  const behind = om.textNode('2', 'Body 12/Bold', 'Accent', 1, 'Behind'); put(behind, 20, 14 - behind.height / 2);
  put(icon('up', 16, 'Icon up'), 36, 6);
  const ahead = om.textNode('1', 'Body 12/Bold', 'Accent', 1, 'Ahead'); put(ahead, 52, 14 - ahead.height / 2);
  const syncComp = figma.createComponentFromNode(sd); cframe.appendChild(syncComp);
  om.child(syncComp, 'Behind').componentPropertyReferences = { characters: syncComp.addComponentProperty('Behind', 'TEXT', '2') };
  om.child(syncComp, 'Ahead').componentPropertyReferences = { characters: syncComp.addComponentProperty('Ahead', 'TEXT', '1') };

  // ---- SectionLabel
  const sl = hbox('SectionLabel', { h: 16, px: 0, gap: 0 }); // a 16 px text line, the caption centred
  const slt = om.textNode('MESSAGE', 'Caption 10/Bold', 'Muted'); slt.letterSpacing = { value: 0.8, unit: 'PIXELS' }; sl.appendChild(slt);
  const sectionComp = figma.createComponentFromNode(sl); cframe.appendChild(sectionComp);
  om.child(sectionComp, 'Label').componentPropertyReferences = { characters: sectionComp.addComponentProperty('Label', 'TEXT', 'MESSAGE') };

  // ---- lay out the top of the Components frame, drop the flat originals
  const captions = cframe.findOne(n => n.name === 'Captions' && n.type === 'GROUP');
  const isPatternCaption = n => /^caption (Tables|Diff|Mini)/.test(n.name);
  const oldCaptions = captions.children.filter(n => !isPatternCaption(n));
  const dimText = cframe.findOne(n => n.name === 'DimText' && n.type === 'COMPONENT');
  const rows = [
    ['Button · variant × state · props: Label, Icon, Show icon / label / chevron / badge / mark · Chip/branch · Chip/repo · Badge · SyncDropdown · IconButton (variant ghost / default, state, size 24 / 28, Icon)', [buttonSet, chipBranch, chipRepo, badgeComp, syncComp, iconButtonSet]],
    ['Icon · Material Design Icons, 16 px box with the glyph at 14 (7/8), glyph scales with the instance', [iconSet]],
    ['Segment (state) · Segmented (mode hug / stretch, Show segment 3) · Field (state, Value, Placeholder, Icon, Caret, Trailing icon)', [segmentSet, segmentedSet, fieldSet]],
    ['Checkbox (state, Label) · RefChip (kind, Label) · StatusPill (status) · SectionLabel (Label) · DimText', [checkboxSet, refSet, pillSet, sectionComp, dimText].filter(Boolean)],
  ];
  let y = 70;
  for (const [cap, nodes] of rows) {
    let x = 40, rowH = 0;
    for (const n of nodes) { cframe.appendChild(n); n.x = x; n.y = y; x += n.width + 40; rowH = Math.max(rowH, n.height); }
    const t = om.textNode(cap, 'Small 11/Regular', 'Muted', 1, 'caption ' + cap); captions.appendChild(t); t.x = 40; t.y = y - 20;
    y += rowH + 44;
  }
  // pattern rows (tables, diff, cards) at fixed offsets below the new rows, as in sheets.js
  const capOf = re => captions.children.find(n => re.test(n.name));
  const PATTERN = [[/^caption Tables/, 0, { ChangesTable: 14, CommitsTable: 14 }], [/^caption Diff/, 200, { DiffPane: 14 }],
    [/^caption Mini/, 500, { MiniRail: 14, MenuCard: 14, MergeDialog: 14, Tooltip: 14, 'StatusLine/busy': 54, 'StatusLine/error': 78, Scrollbar: 14, SplitterHandle: 54, Keycap: 14 }]];
  const tablesCap = y + 20;
  for (const [re, off, members] of PATTERN) {
    const cap = capOf(re); if (cap) cap.y = tablesCap + off;
    for (const [nm, dy] of Object.entries(members)) { const n = cframe.findOne(k => k.name === nm && k.type === 'COMPONENT'); if (n) n.y = tablesCap + off + dy; else om.say('pattern missing', nm); }
  }
  // drop the flat originals; a component whose instances live in another old set frees up on a later pass
  let pending = old.slice();
  for (let pass = 0; pass < 4 && pending.length; pass++) { const next = []; for (const n of pending) { const used = n.type === 'COMPONENT' ? n.instances.length : n.children.some(c => c.instances.length); if (used) next.push(n); else n.remove(); } pending = next; }
  pending.forEach(n => om.say('kept (has instances)', n.name));
  oldCaptions.forEach(n => n.remove());
  const H = tablesCap + 500 + 14 + 330 + 40;
  cframe.resizeWithoutConstraints(cframe.width, H); const bg = cframe.findOne(n => n.name === 'bg' && n.parent === cframe); if (bg) bg.resizeWithoutConstraints(cframe.width, H);
  figma.viewport.scrollAndZoomIntoView([buttonSet, checkboxSet]);
  om.say('done; rows end at', y, 'frame height', H, 'removed', old.length - pending.length);
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
