// Re-imports frames in place: `await window.__omReimport(ids)` sets each frame
// aside, imports its new SVG (import-frames.js), swaps the groups for instances
// (replace-instances.js), then puts the new frame where the old one stood —
// the same parent (a page or a section), the same place in the layers, the
// same x / y — carries the prototype links over (reactions pointing at the old
// frame, the old frame's own instances' reactions matched by layer name, flow
// starting points) and removes the old frame. Run after lib.js with
// import-frames.js, replace-instances.js, frames.json, manifest.json,
// icons.json and the frames' SVGs uploaded. Never for the Components sheet.
window.__omReimport = async ids => {
  const om = window.__om; await om.init();
  const pages = figma.root.children;
  for (const p of pages) await p.loadAsync();
  const say = [];
  const olds = [];
  for (const id of ids) {
    const hits = pages.flatMap(p => p.findAll(n => n.type === 'FRAME' && n.name === id && (n.parent.type === 'PAGE' || n.parent.type === 'SECTION')));
    if (hits.length !== 1) { say.push('skip ' + id + ' (' + hits.length + ' found)'); continue; }
    const f = hits[0];
    olds.push({ id, node: f, oldId: f.id, parent: f.parent, x: f.x, y: f.y });
    f.name = id + ' (old)';
  }
  if (!olds.length) return say.join('\n');
  window.__omImport = olds.map(o => o.id); window.__omReplace = false;
  say.push(String(await eval(await om.readFile('import-frames.js'))).split('\n').slice(-1)[0]);
  window.__omOnly = olds.map(o => o.id);
  say.push(String(await eval(await om.readFile('replace-instances.js'))).split('\n').slice(-3).join(' | '));

  // The links: every reaction in the file that navigates to an old frame, and
  // the old frames' own reactions, by the reacting node's layer name.
  const remap = {};
  const fresh = {};
  for (const o of olds) {
    const n = pages.flatMap(p => p.children.filter(c => c.type === 'FRAME' && c.name === o.id))[0];
    if (!n) { say.push('not imported ' + o.id); continue; }
    fresh[o.id] = n; remap[o.oldId] = n.id;
  }
  const acts = r => r.actions || (r.action ? [r.action] : []);
  const navigates = n => n.reactions.some(r => acts(r).some(a => a && a.type === 'NODE' && a.navigation === 'NAVIGATE'));
  const retarget = r => { const { action, ...rest } = r; return { ...rest, actions: acts(r).map(a => a && a.destinationId && remap[a.destinationId] ? { ...a, destinationId: remap[a.destinationId] } : a) }; };
  const oldIds = new Set(olds.map(o => o.node.id));
  const inOld = n => { for (let a = n; a; a = a.parent) if (oldIds.has(a.id)) return true; return false; };
  let links = 0;
  for (const p of pages) {
    for (const n of p.findAll(n => 'reactions' in n && n.reactions.length && n.reactions.some(r => acts(r).some(a => a && remap[a.destinationId])))) {
      if (inOld(n)) continue;
      await n.setReactionsAsync(n.reactions.map(retarget)); links++;
    }
  }
  for (const o of olds) {
    const n = fresh[o.id]; if (!n) continue;
    for (const src of o.node.findAll(m => 'reactions' in m && m.reactions.length && navigates(m))) {
      const dst = n.findOne(m => m.name === src.name && 'reactions' in m);
      if (!dst) { say.push('link lost: ' + o.id + ' / ' + src.name); continue; }
      await dst.setReactionsAsync(src.reactions.map(retarget)); links++;
    }
  }
  // Flow starting points follow their frames; setting a reaction may have
  // started a "Flow n" of its own on a new frame, which gives way to the named one.
  for (const p of pages) {
    const flows = (p.flowStartingPoints || []).map(f => remap[f.nodeId] ? { ...f, nodeId: remap[f.nodeId] } : f);
    if (!flows.some((f, i) => f.nodeId !== (p.flowStartingPoints[i] || {}).nodeId)) continue;
    const named = flows.filter(f => !/^Flow \d+$/.test(f.name)), seen = new Set(named.map(f => f.nodeId));
    p.flowStartingPoints = [...named, ...flows.filter(f => /^Flow \d+$/.test(f.name) && !seen.has(f.nodeId) && (seen.add(f.nodeId), true))];
  }

  // The new frame into the old one's place, the old one gone.
  for (const o of olds) {
    const n = fresh[o.id]; if (!n) continue;
    const index = o.parent.children.indexOf(o.node);
    o.node.remove();
    o.parent.insertChild(index, n);
    n.x = o.x; n.y = o.y;
  }
  say.push('reimported ' + Object.keys(fresh).length + ', links ' + links);
  return say.join('\n').replace(/=/g, '≈');
};
'reimport ready';
