// One-off (2026-09-20): popup search prompts follow the Omarchy menu (Walker)
// look — no box. Adds a `state=prompt` variant to the `Field` set (no fill, no
// border, icon and text on the row grid: icon 10 px in, label 22 px after it)
// and switches the menu-card search fields ("Search branches…", "Search
// repositories…") to it, with a hairline under the prompt. The popup frame is
// the focus cue there; inline fields (history filter, clone dialog) keep the
// bordered normal/focus states. Self-contained: needs no lib.js, no manifest.
(async () => {
  const log = [], say = (...a) => log.push(a.join(' '));
  for (const st of ['Regular', 'Bold']) await figma.loadFontAsync({ family: 'JetBrains Mono', style: st });
  const key = (n, name) => Object.keys(n.componentProperties || {}).find(k => k === name || k.startsWith(name + '#'));
  const comps = figma.root.children.find(p => p.name === 'Components');
  await comps.loadAsync();
  const set = comps.findOne(n => n.type === 'COMPONENT_SET' && n.name === 'Field');
  let prompt = set.children.find(c => c.name === 'state=prompt');
  if (!prompt) {
    const focus = set.children.find(c => c.name === 'state=focus');
    prompt = focus.clone();
    set.appendChild(prompt);
    prompt.name = 'state=prompt';
    for (const ch of [...prompt.children]) if (ch.name === 'fill' || ch.name === 'border') ch.remove();
    // a clone made outside the set loses its property references
    for (const ch of prompt.children) { const src = focus.children.find(s => s.name === ch.name); if (src?.componentPropertyReferences) ch.componentPropertyReferences = src.componentPropertyReferences; }
    prompt.paddingLeft = 10; prompt.itemSpacing = 8;
    say('variant added');
  }
  const fg = set.children[0].children.find(ch => ch.name === 'fill').fills;
  let swapped = 0, lines = 0;
  for (const page of figma.root.children) {
    if (page.name === 'Reference · current app') continue;
    await page.loadAsync();
    for (const n of page.findAll(n => n.type === 'INSTANCE' && n.mainComponent?.parent === set)) {
      if (!/^Search /.test(n.componentProperties[key(n, 'Placeholder')].value)) continue;
      if (n.componentProperties.state.value !== 'prompt') { n.setProperties({ state: 'prompt' }); swapped++; }
      const p = n.parent;
      if (p.children.some(ch => ch.name === 'PromptRule')) continue;
      const r = figma.createRectangle();
      r.name = 'PromptRule';
      p.insertChild(p.children.indexOf(n) + 1, r);
      r.x = n.x + 4; r.y = n.y + n.height + 3; r.resize(n.width - 8, 1);
      r.fills = fg; r.fills = fg; r.opacity = 0.12;
      lines++;
    }
  }
  say('instances switched', swapped, 'rules added', lines);
  return log.join('\n').replace(/=/g, '≈');
})();
