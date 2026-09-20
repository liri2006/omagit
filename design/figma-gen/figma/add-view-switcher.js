// Adds the files-view switcher (three IconButton instances: table, compact,
// tree) to every existing frame whose manifest records it but which was
// imported before it existed, and moves the "5 / 7 selected" count left of it.
// Run after lib.js with manifest.json uploaded; frames that already carry a
// FilesView/* instance are left alone.
(async () => {
  const om = window.__om; om.log = []; await om.init();
  const M = JSON.parse(await om.readFile('manifest.json'));
  const cpage = om.page('Components');
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const iconButton = cframe.findOne(n => n.type === 'COMPONENT_SET' && n.name === 'IconButton');
  const iconSet = cframe.findOne(n => n.type === 'COMPONENT_SET' && n.name === 'Icon');
  const iconId = name => { const c = iconSet.children.find(c => c.name === 'name=' + name); if (!c) om.say('  no icon', name); return c && c.id; };
  let frames = 0, added = 0, moved = 0;
  for (const [fname, { theme, rec }] of Object.entries(M)) {
    const recs = rec.filter(r => /^FilesView\//.test(r.id)); if (!recs.length) continue;
    const frame = om.frame(fname) || cframe.findOne(n => n.name === fname && (n.type === 'FRAME' || n.type === 'COMPONENT')); if (!frame) { om.say('no frame', fname); continue; }
    if (frame.findOne(n => n.type === 'INSTANCE' && /^FilesView\//.test(n.name))) continue;
    { let pg = frame; while (pg.type !== 'PAGE') pg = pg.parent; figma.currentPage = pg; }
    const fb = frame.absoluteBoundingBox;
    const left = Math.min(...recs.map(r => r.x)), top = recs[0].y;
    // the count text on the same header row hosts the buttons (same parent group)
    const counts = frame.findAll(n => n.type === 'TEXT' && n.characters === '5 / 7 selected').map(n => ({ n, d: Math.abs(n.absoluteBoundingBox.y - (fb.y + top)) })).sort((a, b) => a.d - b.d);
    const count = counts.length && counts[0].d < 30 ? counts[0].n : null;
    const parent = count ? count.parent : frame;
    for (const r of recs) {
      const inst = iconButton.defaultVariant.createInstance(); inst.name = r.id;
      om.setProps(inst, { variant: 'ghost', state: r.state, size: '24', Icon: iconId(r.icon) });
      parent.appendChild(inst);
      const at = inst.absoluteTransform; inst.x += (fb.x + r.x) - at[0][2]; inst.y += (fb.y + r.y) - at[1][2];
      if (theme === 'light') om.retheme(inst, 'Light');
      added++;
    }
    if (count) { const tb = count.absoluteBoundingBox; count.x += (fb.x + left - 12) - (tb.x + tb.width); moved++; }
    else om.say('no count text in', fname);
    frames++;
  }
  om.say('frames', frames, 'buttons', added, 'counts moved', moved);
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
