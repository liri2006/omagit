// One-off (2026-09-24): moves the file onto the 4 px grid. The control
// components change anatomy (16 px icon boxes, 8 px padding, 4 px gaps, 12 px
// badges, 16 px checkboxes), so the Components frame is rebuilt from the new
// sheet instead of patched. Run after lib.js with every file of out/ and the
// other scripts uploaded; installs window.__omGrid:
//   await __omGrid.prepare()   renames the old 'Components' frame, imports the new
//                              sheet as a flat frame in its place, drops its flat
//                              control rows and turns the pattern groups into components
//   then build-components.js, then replace-instances.js with __omOnly = ['Components']
//   await __omGrid.batch(ids)  re-imports frames in place (import-frames.js +
//                              replace-instances.js), keeping their layer order
//   __omGrid.finish()          deletes the old frame once nothing outside it uses its components
(() => {
  const om = window.__om, G = (window.__omGrid = {});
  const run = async name => (0, eval)(await om.readFile(name));
  const OLD = 'Components · before grid';
  const PATTERNS = ['ChangesTable', 'CommitsTable', 'DiffPane', 'MiniRail', 'MenuCard', 'MergeDialog', 'Tooltip', 'StatusLine/busy', 'StatusLine/error', 'Scrollbar', 'SplitterHandle', 'Keycap'];

  G.prepare = async () => {
    om.log = []; await om.init();
    const frames = JSON.parse(await om.readFile('frames.json'));
    const cpage = om.page('Components');
    if (cpage.children.find(n => n.name === OLD)) return 'already prepared';
    const old = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
    if (!old) return 'no Components frame';
    old.name = OLD;
    const node = figma.createNodeFromSvg(await om.readFile(frames.find(f => f.id === 'Components').file));
    node.name = 'Components'; cpage.appendChild(node);
    node.x = old.x; node.y = old.y; old.x = old.x + old.width + 400;
    // the flat control rows above the Tables caption come back as real components (build-components.js)
    const group = node.children.find(n => n.name === 'Components' && n.type === 'GROUP');
    const capY = node.findOne(n => n.name.startsWith('caption Tables')).y;
    let dropped = 0;
    for (const n of [...group.children]) if (n.y < capY) { n.remove(); dropped++; }
    // the pattern rows become components, as they were in the old frame
    let made = 0;
    for (const name of PATTERNS) {
      const g = group.children.find(n => n.name === name);
      if (!g) { om.say('no pattern', name); continue; }
      figma.createComponentFromNode(g); made++;
    }
    om.say('prepared: dropped', dropped, 'flat controls, made', made, 'pattern components');
    return om.out();
  };

  // Re-import `ids` in place and swap their controls for instances; frames keep
  // their index in the page's layer list (import-frames.js appends to the page).
  G.batch = async ids => {
    const pos = [];
    for (const id of ids) { const f = om.frame(id); if (f) pos.push({ id, page: f.parent, index: f.parent.children.indexOf(f) }); }
    window.__omReplace = true; window.__omImport = ids;
    const a = await run('import-frames.js');
    window.__omOnly = ids;
    const b = await run('replace-instances.js');
    pos.sort((p, q) => p.index - q.index);
    for (const { id, page, index } of pos) {
      const f = page.children.find(n => n.name === id && n.type === 'FRAME');
      if (f) page.insertChild(Math.min(index, page.children.length - 1), f);
    }
    window.__omReplace = false; window.__omImport = null; window.__omOnly = null;
    return (a + '\n' + b).split('\n').filter(l => !/^(frame|removed|component) /.test(l)).join('\n');
  };

  G.finish = () => {
    const old = om.page('Components').children.find(n => n.name === OLD);
    if (!old) return 'no old frame';
    const outside = [];
    for (const c of old.findAll(n => n.type === 'COMPONENT')) for (const i of c.instances) {
      let p = i, inside = false; while (p) { if (p === old) { inside = true; break; } p = p.parent; }
      if (!inside) outside.push(c.name + ' <- ' + i.name);
    }
    if (outside.length) return 'still used ' + outside.length + ': ' + outside.slice(0, 20).join(' | ');
    old.remove();
    return 'removed the old Components frame';
  };
  return 'grid-rebuild ready';
})();
