// One-off (2026-09-26): the two-row (extra narrow) top bar's sync dropdown is a
// borderless miniature — no fill, no border, no chevron, the counts against
// their arrows' boxes: [4][↓ 16][2][8][↑ 16][1][4], 64 wide (screens.js
// syncDropdown(…, mini)). Adds the `SyncDropdown/mini` component (Behind /
// Ahead text properties, like SyncDropdown) under SyncDropdown on the
// Components sheet and names it in the row's caption; replace-instances.js
// places it for manifest records with `mini: true`. Run after lib.js;
// re-runnable (an existing one is left alone).
(async () => {
  const om = window.__om; om.log = []; await om.init();
  const cpage = om.page('Components'); await cpage.loadAsync();
  const cframe = cpage.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const top = n => (n.type === 'COMPONENT_SET' || n.type === 'COMPONENT') && n.parent.type !== 'COMPONENT_SET';
  const sync = cframe.findOne(n => top(n) && n.name === 'SyncDropdown');
  const iconSet = cframe.findOne(n => n.type === 'COMPONENT_SET' && n.name === 'Icon');
  if (cframe.findOne(n => top(n) && n.name === 'SyncDropdown/mini')) { om.say('already there'); return; }
  const f = figma.createFrame(); f.name = 'SyncDropdown/mini'; f.fills = []; f.strokes = []; f.clipsContent = false; f.cornerRadius = 0;
  f.resizeWithoutConstraints(64, 28);
  const put = (n, x, y) => { f.appendChild(n); n.x = x; n.y = y; };
  const icon = (name, nodeName) => { const i = iconSet.children.find(c => c.name === 'name=' + name).createInstance(); i.name = nodeName; return i; };
  put(icon('down', 'Icon down'), 4, 6);
  const behind = om.textNode('2', 'Body 12/Bold', 'Accent', 1, 'Behind'); put(behind, 20, 14 - behind.height / 2);
  put(icon('up', 'Icon up'), 36, 6);
  const ahead = om.textNode('1', 'Body 12/Bold', 'Accent', 1, 'Ahead'); put(ahead, 52, 14 - ahead.height / 2);
  const comp = figma.createComponentFromNode(f); cframe.appendChild(comp);
  comp.x = sync.x; comp.y = sync.y + sync.height + 12;
  om.child(comp, 'Behind').componentPropertyReferences = { characters: comp.addComponentProperty('Behind', 'TEXT', '2') };
  om.child(comp, 'Ahead').componentPropertyReferences = { characters: comp.addComponentProperty('Ahead', 'TEXT', '1') };
  const cap = cframe.findOne(n => n.type === 'TEXT' && /^caption Button · variant/.test(n.name));
  if (cap && !/SyncDropdown \(\+ \/mini\)/.test(cap.characters)) { await figma.loadFontAsync(cap.fontName); cap.characters = cap.characters.replace('· SyncDropdown ·', '· SyncDropdown (+ /mini) ·'); }
  om.say('SyncDropdown/mini at', comp.x, comp.y, 'caption', cap ? 'updated' : 'not found');
})().then(() => window.__om.out()).catch(e => 'FAILED ' + e.message + '\n' + (e.stack || '').slice(0, 600) + '\n' + window.__om.out());
