// One-off (2026-09-26): the footer's settings cog, left of the keys button,
// as the app's footer has it since the same day. Every screen's footer gets a
// copy of its keyboard IconButton, an item gap (8) plus its own 24 px further
// left, named after the manifest id 'Settings', with the Icon swapped to the
// cog; a Light frame's copy keeps the Light overrides of the one it is copied
// from. Idempotent: a footer that has its cog already is left alone.
// Self-contained, like fold-count.js: no lib.js, no manifest.
(async () => {
  const log = [], say = (...a) => log.push(a.join(' '));
  const SKIP = ['Cover', 'Foundations', 'Components', 'Layout rules', 'Reference · current app'];
  const key = (n, name) => Object.keys(n.componentProperties || {}).find(k => k === name || k.startsWith(name + '#'));
  const iconOf = n => { const k = key(n, 'Icon'); return k ? n.componentProperties[k].value : null; };
  const frameOf = n => { let f = n; while (f.parent && f.parent.type !== 'PAGE' && f.parent.type !== 'SECTION') f = f.parent; return f.name; };
  const cpage = figma.root.children.find(p => p.name === 'Components');
  await cpage.loadAsync();
  const iconSet = cpage.findOne(n => n.type === 'COMPONENT_SET' && n.name === 'Icon');
  const glyph = name => iconSet.children.find(c => c.name === 'name=' + name);
  const cog = glyph('cog'), keyboard = glyph('keyboard');
  if (!cog || !keyboard) return 'no cog / keyboard icon in the Icon set';
  let patched = 0, already = 0;
  const missing = [];
  for (const page of figma.root.children) {
    if (SKIP.includes(page.name)) continue;
    await page.loadAsync();
    for (const footer of page.findAll(n => n.name === 'Footer')) {
      const buttons = footer.findAll(n => n.type === 'INSTANCE' && key(n, 'Icon'));
      const keys = buttons.find(b => iconOf(b) === keyboard.id);
      if (!keys) { missing.push(frameOf(footer)); continue; }
      if (buttons.some(b => iconOf(b) === cog.id)) { already++; continue; }
      const settings = keys.clone();
      keys.parent.insertChild(keys.parent.children.indexOf(keys), settings);
      settings.x = keys.x - keys.width - 8; settings.y = keys.y;
      settings.name = 'Settings';
      settings.setProperties({ [key(settings, 'Icon')]: cog.id });
      patched++;
    }
  }
  say('footers patched', patched, 'already had the cog', already, 'without a keys button', missing.length, missing.join(' | '));
  return log.join('\n').replace(/=/g, '≈');
})();
