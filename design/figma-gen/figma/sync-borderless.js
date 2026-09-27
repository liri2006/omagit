// One-off (2026-09-27): the stacked top bar's sync dropdown is one borderless
// form on one row and on two — no chrome, no chevron, the counts against their
// arrows' boxes: [4][↓ 16][2][8][↑ 16][1][6], 66 wide (screens.js
// syncDropdown()). The 6 after the last count leaves as much bare room after
// its ink as there is before the down arrow's. Reshapes the `SyncDropdown`
// component in place, so every instance follows; with
// `window.__omSyncFinish = true` it instead deletes `SyncDropdown/mini` once
// no instance uses it and names the plain component in the row's caption.
// Run after lib.js; re-runnable.
(async () => {
  const om = window.__om; om.log = []; await om.init();
  const cpage = om.page('Components'); await cpage.loadAsync();
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const top = n => (n.type === 'COMPONENT_SET' || n.type === 'COMPONENT') && n.parent.type !== 'COMPONENT_SET';
  const sync = cframe.findOne(n => top(n) && n.name === 'SyncDropdown');
  const mini = cframe.findOne(n => top(n) && n.name === 'SyncDropdown/mini');
  if (window.__omSyncFinish) {
    if (mini) {
      const uses = await mini.getInstancesAsync();
      if (uses.length) om.say('SyncDropdown/mini kept:', uses.length, 'instances left');
      else { mini.remove(); om.say('SyncDropdown/mini removed'); }
    }
    const cap = cframe.findOne(n => n.type === 'TEXT' && /^caption Button · variant/.test(n.name));
    if (cap && cap.characters.includes('SyncDropdown (+ /mini)')) { await figma.loadFontAsync(cap.fontName); cap.characters = cap.characters.replace('SyncDropdown (+ /mini)', 'SyncDropdown'); om.say('caption updated'); }
    return;
  }
  // The chrome and the chevron go; the fields move to the borderless form's
  // places, absolute in a frame that no longer lays them out.
  for (const name of ['fill', 'border', 'Chevron']) { const n = sync.children.find(c => c.name === name); if (n) n.remove(); }
  const at = { 'Icon down': 4, Behind: 20, 'Icon up': 36, Ahead: 52 };
  const ys = {};
  for (const c of sync.children) ys[c.name] = c.y;
  sync.layoutMode = 'NONE';
  sync.fills = []; sync.strokes = [];
  sync.resizeWithoutConstraints(66, 28);
  for (const c of sync.children) {
    if (!(c.name in at)) { om.say('unexpected child', c.name); continue; }
    if ('layoutPositioning' in c) try { c.layoutPositioning = 'AUTO'; } catch (e) { /* not in auto layout any more */ }
    c.x = at[c.name]; c.y = ys[c.name];
  }
  om.say('SyncDropdown', sync.width + 'x' + sync.height, 'children', sync.children.map(c => c.name + '@' + c.x + ',' + Math.round(c.y)).join(' '));
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
