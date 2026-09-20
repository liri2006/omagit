// One-off (2026-09-18): folds the "5 / 7 selected" count into the CHANGES
// section title ("CHANGES · 5/7") and puts the count on the Commit button
// ("Commit 5 files") in every frame and pattern component. Self-contained:
// needs no lib.js and no manifest — the sample data is the same everywhere.
// The right-aligned Commit buttons (wide layouts, the commit popover) were
// imported as fixed-width instances, so they are switched to hug their new
// label and moved left to keep their right edge; the stretched stacked-layout
// buttons just relabel.
(async () => {
  const log = [], say = (...a) => log.push(a.join(' '));
  for (const st of ['Regular', 'Bold']) await figma.loadFontAsync({ family: 'JetBrains Mono', style: st });
  const key = (n, name) => Object.keys(n.componentProperties || {}).find(k => k === name || k.startsWith(name + '#'));
  const label = n => { const k = key(n, 'Label'); return k && n.componentProperties[k].value; };
  const RELABEL = { 'CHANGES': 'CHANGES · 5/7', 'Commit': 'Commit 5 files', 'Commit  ⏎': 'Commit 5 files  ⏎' };
  const compsFrame = figma.root.children.find(p => p.name === 'Components')?.children.find(n => n.name === 'Components' && n.type === 'FRAME');
  const inComps = n => { for (let p = n.parent; p; p = p.parent) if (p === compsFrame) return true; return false; };
  let titles = 0, buttons = 0, removed = 0, renamed = 0;
  for (const page of figma.root.children) {
    if (page.name === 'Reference · current app') continue;
    for (const n of page.findAll(n => n.type === 'INSTANCE')) {
      const to = RELABEL[label(n)]; if (!to) continue;
      const w0 = n.width;
      n.setProperties({ [key(n, 'Label')]: to });
      if (/^CHANGES/.test(to)) { titles++; continue; }
      buttons++;
      if (/⏎$/.test(to) && n.layoutSizingHorizontal === 'FIXED') n.layoutSizingHorizontal = 'HUG';
      const dw = n.width - w0; if (dw) n.x -= dw;
    }
    for (const t of page.findAll(n => n.type === 'TEXT' && n.characters === '5 / 7 selected')) {
      if (inComps(t)) { t.characters = 'Nothing to commit'; renamed++; } else { t.remove(); removed++; }
    }
  }
  say('titles', titles, 'buttons', buttons, 'counts removed', removed, 'sample renamed', renamed);
  return log.join('\n').replace(/=/g, '≈');
})();
