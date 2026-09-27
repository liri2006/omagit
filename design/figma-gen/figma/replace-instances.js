// Swaps the flat SVG groups of every frame for instances of the rebuilt
// components, driven by out/manifest.json (kind, rect, props per placement).
// Set window.__omOnly = ['Wide · Changes'] to limit the run to some frames.
(async () => {
  const om = window.__om; om.log = []; await om.init();
  const M = JSON.parse(await om.readFile('manifest.json'));
  const cpage = om.page('Components');
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const comp = {};
  cframe.findAll(n => (n.type === 'COMPONENT_SET' || n.type === 'COMPONENT') && n.parent.type !== 'COMPONENT_SET').forEach(n => { comp[n.name] = n; });
  const iconId = name => { const c = comp.Icon.children.find(c => c.name === 'name=' + name); if (!c) om.say('  no icon', name); return c && c.id; };
  const only = window.__omOnly;
  const stats = { frames: 0, replaced: 0, missing: [] };
  const patternTop = Math.min(...cframe.findAll(n => /^caption (Tables|Diff|Mini)/.test(n.name)).map(n => n.y));

  const make = r => {
    switch (r.k) {
      case 'Button': {
        if (r.icon && !r.label && r.fixed && comp.IconButton) { // square icon-only buttons are IconButton instances
          const inst = comp.IconButton.defaultVariant.createInstance(); inst.name = r.id;
          om.setProps(inst, { variant: r.variant === 'default' ? 'default' : 'ghost', state: r.state, size: r.h === 24 ? '24' : '28', Icon: iconId(r.icon) });
          if (r.w !== r.h || (r.h !== 24 && r.h !== 28)) inst.resize(r.w, r.h);
          return inst;
        }
        const chip = r.id === 'BranchChip' ? comp['Chip/branch'] : r.id === 'RepoChip' ? comp['Chip/repo'] : null;
        const inst = chip ? chip.createInstance() : comp.Button.defaultVariant.createInstance();
        if (!chip) om.setProps(inst, { variant: r.variant, state: r.state });
        const props = { Label: r.label || 'Label', 'Show label': !!r.label, 'Show icon': !!r.icon, 'Show chevron': !!r.chevron };
        if (r.icon) props.Icon = iconId(r.icon);
        if (!chip) { props['Show badge'] = r.badge !== undefined; props['Show mark'] = !!r.mark; }
        om.setProps(inst, props);
        if (r.badge !== undefined) om.setProps(om.child(inst, 'Badge'), { Count: String(r.badge) });
        if (!chip && r.bold) om.child(inst, 'Label').textStyleId = om.text['Body 12/Bold'].id;
        if (!chip && r.iconAccent) om.fill(om.child(om.child(inst, 'Icon'), 'glyph'), 'Accent');
        if (r.busy) om.fill(om.child(inst, 'Label'), 'Accent');
        if (r.iconSize && r.iconSize !== 16) om.child(inst, 'Icon').resize(r.iconSize, r.iconSize);
        if (r.px !== undefined && r.px !== inst.paddingLeft) { try { inst.paddingLeft = r.px; inst.paddingRight = r.px; } catch (e) { om.say('  padding override failed', e.message); } }
        if (r.fixed) { inst.layoutSizingHorizontal = 'FIXED'; inst.resize(r.w, r.h); }
        else if (r.h !== 28) { inst.resize(inst.width, r.h); inst.layoutSizingHorizontal = 'HUG'; }
        return inst;
      }
      case 'Segmented': {
        const inst = comp.Segmented.defaultVariant.createInstance();
        om.setProps(inst, { mode: r.stretch ? 'stretch' : 'hug', 'Show segment 3': r.items.length > 2 });
        r.items.forEach((it, i) => {
          const s = om.child(inst, 'Segment ' + (i + 1)); if (!s) return;
          const p = { state: it.selected ? 'selected' : 'normal', Label: it.label || 'Label', 'Show label': !!it.label, 'Show icon': !!it.icon, 'Show count': it.count !== undefined, 'Show divider': i > 0 };
          if (it.icon) p.Icon = iconId(it.icon); if (it.count !== undefined) p.Count = String(it.count);
          om.setProps(s, p);
        });
        if (r.stretch) inst.resize(r.w, r.h);
        return inst;
      }
      case 'Field': {
        const inst = comp.Field.defaultVariant.createInstance();
        const p = { state: ['focus', 'prompt'].includes(r.state) ? r.state : 'normal', 'Show icon': !!r.icon, 'Show value': !!r.value, 'Show placeholder': !r.value && !!r.placeholder, 'Show caret': r.state !== 'normal' && !!r.value, 'Show trailing icon': !!r.trailingIcon, Value: r.value || 'Value', Placeholder: r.placeholder || 'Placeholder' };
        if (r.icon) p.Icon = iconId(r.icon); if (r.trailingIcon) p['Trailing icon'] = iconId(r.trailingIcon);
        om.setProps(inst, p); inst.resize(r.w, r.h);
        return inst;
      }
      case 'Checkbox': { const inst = comp.Checkbox.defaultVariant.createInstance(); om.setProps(inst, { state: r.disabled ? 'disabled' : r.state, Label: r.label || 'Label', 'Show label': !!r.label }); return inst; }
      case 'RefChip': { const inst = comp.RefChip.defaultVariant.createInstance(); om.setProps(inst, { kind: r.kind, Label: r.label }); return inst; }
      case 'StatusPill': { const inst = comp.StatusPill.defaultVariant.createInstance(); om.setProps(inst, { status: r.status }); return inst; }
      case 'SyncDropdown': { const inst = comp.SyncDropdown.createInstance(); om.setProps(inst, { Behind: String(r.down), Ahead: String(r.up) }); return inst; }
      case 'SectionLabel': { const inst = comp.SectionLabel.createInstance(); om.setProps(inst, { Label: r.label }); return inst; }
    }
    return null;
  };

  for (const [fname, { theme, rec }] of Object.entries(M)) {
    if (only && !only.includes(fname)) continue;
    const frame = om.frame(fname) || cframe.findOne(n => n.name === fname && (n.type === 'FRAME' || n.type === 'COMPONENT')); if (!frame) { om.say('no frame', fname); continue; }
    { let pg = frame; while (pg.type !== 'PAGE') pg = pg.parent; figma.currentPage = pg; }
    const fb = frame.absoluteBoundingBox;
    const dy = fname === 'Components' ? patternTop - 356 : 0;
    // index flat nodes by name, never descending into instances
    const byName = new Map();
    const walk = n => { for (const c of n.children || []) { if (c.type === 'INSTANCE') continue; const l = byName.get(c.name); if (l) l.push(c); else byName.set(c.name, [c]); walk(c); } };
    walk(frame);
    const used = new Set(); let n = 0;
    for (const r0 of rec) {
      if (fname === 'Components' && r0.y < 350) continue; // the old flat control rows, already gone
      const r = { ...r0, y: r0.y + dy };
      const cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      const cands = (byName.get(r.id) || []).filter(c => !c.removed && !used.has(c.id)).map(c => { const b = c.absoluteBoundingBox; if (!b) return null; const bx = b.x - fb.x + b.width / 2, by = b.y - fb.y + b.height / 2; return { c, d: Math.hypot(bx - cx, by - cy), ok: bx >= r.x - 8 && bx <= r.x + r.w + 8 && by >= r.y - 8 && by <= r.y + r.h + 8 }; }).filter(x => x && x.ok).sort((a, b) => a.d - b.d);
      if (!cands.length) { stats.missing.push(fname + ':' + r.id + '@' + r.x + ',' + r.y); continue; }
      const node = cands[0].c; used.add(node.id);
      let inst; try { inst = make(r); } catch (e) { om.say('make failed', fname, r.id, e.message); continue; }
      if (!inst) continue;
      const parent = node.parent; const idx = parent.children.indexOf(node);
      parent.insertChild(idx, inst);
      const at = inst.absoluteTransform; inst.x += (fb.x + r.x) - at[0][2]; inst.y += (fb.y + r.y) - at[1][2];
      if (theme === 'light') om.retheme(inst, 'Light');
      if (r.id === 'BranchChip') { const ov = parent.children.find(c => c.name === 'BranchChip/label-accent'); if (ov) ov.remove(); }
      node.remove(); n++;
    }
    stats.frames++; stats.replaced += n; om.say(fname, 'replaced', n, 'of', rec.length);
  }
  om.say('missing', stats.missing.length, stats.missing.slice(0, 40).join(' | '));
  om.say('TOTAL frames', stats.frames, 'replaced', stats.replaced);
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
