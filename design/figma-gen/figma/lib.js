// Shared helpers for the Figma page-context scripts. Run first: installs window.__om.
// The Figma editor tab exposes the plugin API as the global `figma`.
const om = (window.__om = window.__om || {});
om.log = [];
om.say = (...a) => om.log.push(a.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' '));
om.out = () => om.log.join('\n').replace(/=/g, '≈'); // '=' pairs trip the tool's output filter
om.ROLE = { bg: 'Background', fg: 'Foreground', accent: 'Accent', dim: 'Muted', red: 'Red', green: 'Green', yellow: 'Yellow', blue: 'Blue', magenta: 'Magenta', cyan: 'Cyan', white: 'White' };
om.init = async () => {
  om.vars = {}; om.varById = {};
  for (const col of figma.variables.getLocalVariableCollections()) {
    const theme = /Dark/.test(col.name) ? 'Dark' : 'Light';
    for (const id of col.variableIds) { const v = figma.variables.getVariableById(id); om.vars[theme + '/' + v.name] = v; om.varById[v.id] = theme + '/' + v.name; }
  }
  om.styles = {}; om.styleById = {};
  for (const s of figma.getLocalPaintStyles()) { om.styles[s.name] = s; om.styleById[s.id] = s.name; }
  om.text = {}; for (const s of figma.getLocalTextStyles()) om.text[s.name] = s;
  for (const st of ['Regular', 'Bold']) await figma.loadFontAsync({ family: 'JetBrains Mono', style: st });
  om.page = name => figma.root.children.find(p => p.name === name);
  om.frame = name => { for (const p of figma.root.children) { const f = p.children.find(n => n.name === name); if (f) return f; } return null; };
};
// Paint bound to a theme variable; opacity lives on the paint (a style would force it to 100 %).
// A variable-bound paint keeps its opacity only when it is written complete
// (visible, blendMode, the variable's resolved colour, boundVariables); any
// partial paint, including what setBoundVariableForPaint returns, snaps to 100 %.
om.varColor = v => { const c = Object.values(v.valuesByMode)[0]; return { r: c.r, g: c.g, b: c.b }; };
om.paintFor = (v, opacity = 1) => ({ type: 'SOLID', visible: true, blendMode: 'NORMAL', color: om.varColor(v), opacity, boundVariables: { color: { type: 'VARIABLE_ALIAS', id: v.id } } });
om.paint = (theme, role, opacity = 1) => om.paintFor(om.vars[theme + '/' + role], opacity);
// Assigning a bound paint to a node snaps its opacity to 100 % the first time
// (Figma treats it as "apply variable"); assigning the same paint again is an
// update and keeps the opacity. Hence every write goes twice.
om.setFills = (node, paints) => { node.fills = paints; node.fills = paints; };
om.setStrokes = (node, paints) => { node.strokes = paints; node.strokes = paints; };
// Fill helper: opaque fills use the paint style, translucent ones the variable.
om.fill = (node, role, opacity = 1, theme = 'Dark') => {
  if (opacity >= 1) { try { node.fillStyleId = om.styles[theme + '/' + role].id; return; } catch (e) { /* fall through */ } }
  om.setFills(node, [om.paint(theme, role, opacity)]);
};
om.stroke = (node, role, opacity = 1, weight = 1, theme = 'Dark') => {
  om.setStrokes(node, [om.paint(theme, role, opacity)]); node.strokeWeight = weight; node.strokeAlign = 'INSIDE';
};
// Opaque stroke through the paint style (translucency goes on the node's layer opacity instead).
om.strokeStyle = (node, role, weight = 1, theme = 'Dark') => {
  try { node.strokeStyleId = om.styles[theme + '/' + role].id; } catch (e) { om.setStrokes(node, [om.paint(theme, role, 1)]); }
  node.strokeWeight = weight; node.strokeAlign = 'INSIDE';
};
// Instances render variable-bound paints at 100 % whatever the paint opacity
// says, so translucent chrome is a child rect with layer opacity: 'fill' and
// 'border' absolute rects stretched over an auto-layout frame.
om.chrome = (f, { fillRole = 'Foreground', fo = 0, strokeRole = 'Foreground', so = 0, weight = 1 } = {}) => {
  const fr = figma.createRectangle(); fr.name = 'fill'; om.fill(fr, fillRole); f.appendChild(fr);
  fr.layoutPositioning = 'ABSOLUTE'; fr.constraints = { horizontal: 'STRETCH', vertical: 'STRETCH' }; fr.x = 0; fr.y = 0; fr.opacity = fo; fr.visible = fo > 0;
  const br = figma.createRectangle(); br.name = 'border'; br.fills = []; om.strokeStyle(br, strokeRole, weight); f.appendChild(br);
  br.layoutPositioning = 'ABSOLUTE'; br.constraints = { horizontal: 'STRETCH', vertical: 'STRETCH' }; br.x = 0; br.y = 0; br.opacity = so; br.visible = so > 0;
  return { fr, br };
};
om.fitChrome = f => { for (const c of f.children) if (c.name === 'fill' || c.name === 'border') c.resize(f.width, f.height); };
om.textNode = (chars, styleName, role = 'Foreground', opacity = 1, name = 'Label') => {
  const t = figma.createText(); t.name = name;
  try { t.textStyleId = om.text[styleName].id; } catch (e) { const [fam, sz] = styleName.split('/'); t.fontName = { family: 'JetBrains Mono', style: sz }; t.fontSize = +fam.split(' ')[1]; }
  t.characters = chars; t.textAutoResize = 'WIDTH_AND_HEIGHT';
  om.fill(t, role, opacity);
  return t;
};
// Retheme a node tree: rebind every Dark/* fill, stroke or style to Light/* (or back).
om.retheme = (root, to = 'Light') => {
  const from = to === 'Light' ? 'Dark' : 'Light';
  const fix = paints => paints.map(p => {
    const b = p.boundVariables && p.boundVariables.color; if (!b) return p;
    const nm = om.varById[b.id]; if (!nm || !nm.startsWith(from + '/')) return p;
    return om.paintFor(om.vars[to + '/' + nm.slice(from.length + 1)], p.opacity);
  });
  const walk = n => {
    if ('fillStyleId' in n && typeof n.fillStyleId === 'string' && n.fillStyleId) { const nm = om.styleById[n.fillStyleId]; if (nm && nm.startsWith(from + '/')) n.fillStyleId = om.styles[to + '/' + nm.slice(from.length + 1)].id; }
    else if ('fills' in n && Array.isArray(n.fills)) { const f = fix(n.fills); if (JSON.stringify(f) !== JSON.stringify(n.fills)) om.setFills(n, f); }
    if ('strokeStyleId' in n && typeof n.strokeStyleId === 'string' && n.strokeStyleId) { const nm = om.styleById[n.strokeStyleId]; if (nm && nm.startsWith(from + '/')) n.strokeStyleId = om.styles[to + '/' + nm.slice(from.length + 1)].id; }
    else if ('strokes' in n && Array.isArray(n.strokes) && n.strokes.length) { const s = fix(n.strokes); if (JSON.stringify(s) !== JSON.stringify(n.strokes)) om.setStrokes(n, s); }
    (n.children || []).forEach(walk);
  };
  walk(root);
};
// Component property helpers: keys carry an id suffix ("Label#12:3").
om.propKey = (node, name) => Object.keys(node.componentPropertyDefinitions || node.componentProperties || {}).find(k => k === name || k.startsWith(name + '#'));
om.setProps = (inst, props) => { const o = {}; for (const [k, v] of Object.entries(props)) { const key = om.propKey(inst, k); if (key) o[key] = v; else om.say('  no prop', k, 'on', inst.name); } if (Object.keys(o).length) inst.setProperties(o); };
om.child = (node, name) => node.findOne(n => n.name === name);
om.readFile = async name => { const inp = document.getElementById('__omfile'); const f = [...inp.files].find(f => f.name === name); if (!f) throw new Error('file not uploaded: ' + name); return f.text(); };
om.say('lib ready; vars', 0, 'styles', 0);
'lib loaded';
