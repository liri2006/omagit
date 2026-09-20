// Imports frames from out/*.svg that the file does not have yet (or the ones
// listed in window.__omImport), places them after the last frame of their
// page, and turns Components-page sheets into components under the pattern
// rows. Run after lib.js with frames.json and the SVGs uploaded; follow with
// replace-instances.js (window.__omOnly is set to the imported ids).
(async () => {
  const om = window.__om; om.log = []; await om.init();
  const frames = JSON.parse(await om.readFile('frames.json'));
  const cpage = om.page('Components');
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const wanted = window.__omImport || frames.filter(f => !om.frame(f.id) && !cframe.findOne(n => n.name === f.id && n.type === 'COMPONENT')).map(f => f.id);
  const imported = [];
  // icons the generator gained since the Icon set was built become new variants
  try {
    const ICONS = JSON.parse(await om.readFile('icons.json'));
    const iconSet = cframe.findOne(n => n.type === 'COMPONENT_SET' && n.name === 'Icon'); let added = 0;
    for (const [name, d] of Object.entries(ICONS)) {
      if (iconSet.children.find(c => c.name === 'name=' + name)) continue;
      const g = figma.createNodeFromSvg(`<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24"><path d="${d}" fill="#000000"/></svg>`);
      g.name = 'name=' + name; g.fills = []; g.clipsContent = false;
      g.findAll(n => n.type === 'VECTOR' || n.type === 'BOOLEAN_OPERATION').forEach(v => { v.name = 'glyph'; om.fill(v, 'Foreground'); v.constraints = { horizontal: 'SCALE', vertical: 'SCALE' }; });
      const comp = figma.createComponentFromNode(g); iconSet.appendChild(comp); added++;
    }
    om.say('icons added', added);
  } catch (e) { om.say('icons skipped:', e.message); }
  const slot = {}; // a replaced frame keeps its place in the row
  if (window.__omReplace) for (const id of wanted) { // re-import: drop the previous copy (and a sheet's caption)
    const old = om.frame(id) || cframe.findOne(n => n.name === id && n.type === 'COMPONENT');
    if (old) { if (old.type === 'COMPONENT' && old.instances.length) om.say('kept, has instances', id); else { slot[id] = { x: old.x, y: old.y }; old.remove(); const cap = cframe.findOne(n => n.name === 'caption ' + id); if (cap) cap.remove(); om.say('removed', id); } }
  }
  for (const f of frames) {
    if (!wanted.includes(f.id)) continue;
    let svg; try { svg = await om.readFile(f.file); } catch (e) { om.say('skip (not uploaded)', f.file); continue; }
    const node = figma.createNodeFromSvg(svg); node.name = f.id;
    if (f.page === 'Components') {
      // a pattern sheet: a component below the last row, caption above it
      const bottom = Math.max(...cframe.children.filter(n => n.name !== 'bg' && n.name !== 'Captions').map(n => n.y + n.height));
      cframe.appendChild(node); node.x = 40; node.y = bottom + 60;
      const comp = figma.createComponentFromNode(node);
      const captions = cframe.findOne(n => n.name === 'Captions' && n.type === 'GROUP');
      const t = om.textNode(f.id + ' · pattern', 'Small 11/Regular', 'Muted', 1, 'caption ' + f.id); (captions || cframe).appendChild(t); t.x = 40; t.y = comp.y - 20;
      const H = comp.y + comp.height + 40;
      if (H > cframe.height) { cframe.resizeWithoutConstraints(cframe.width, H); const bg = cframe.findOne(n => n.name === 'bg' && n.parent === cframe); if (bg) bg.resizeWithoutConstraints(cframe.width, H); }
      om.say('component', f.id, 'at', comp.x, comp.y);
    } else {
      const page = om.page(f.page); if (!page) { om.say('no page', f.page); node.remove(); continue; }
      const others = page.children.filter(n => n.type === 'FRAME');
      page.appendChild(node);
      if (slot[f.id]) { node.x = slot[f.id].x; node.y = slot[f.id].y; }
      else { node.x = others.length ? Math.max(...others.map(n => n.x + n.width)) + 100 : 0; node.y = others.length ? Math.min(...others.map(n => n.y)) : 0; }
      om.say('frame', f.id, 'on', f.page, 'at', node.x, node.y);
    }
    imported.push(f.id);
  }
  window.__omOnly = imported;
  om.say('imported', imported.length);
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
