const fs = require('fs'); const K = require('./kit.js'); const S = require('./screens.js');
const OUT = __dirname + '/out'; fs.rmSync(OUT, { recursive: true, force: true }); fs.mkdirSync(OUT);
const frames = [];
const manifest = {};
function emit(c, page) { const f = `${String(frames.length + 1).padStart(2, '0')}-${c.id.replace(/[^a-z0-9]+/gi, '-')}.svg`; fs.writeFileSync(`${OUT}/${f}`, c.svg()); frames.push({ file: f, id: c.id, page, w: c.w, h: c.h }); manifest[c.id] = { theme: K.theme().mode, rec: c.rec }; }
K.setTheme('dark');
// Wide 1900x1234
emit(S.screen({ id: 'Wide · Changes', W: 1900, H: 1234, page: 'changes' }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · History', W: 1900, H: 1234, page: 'history' }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Message written + Amend', W: 1900, H: 1234, page: 'changes', amend: true, message: 'Make the toolbar tiling aware', messageBody: ['', '- Fold labels, icons and buttons in three steps', '- Pick the window layout from width and height'] }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Merge dialog', W: 1900, H: 1234, page: 'changes', overlay: 'merge' }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Branch menu', W: 1900, H: 1234, page: 'changes', overlay: 'branch' }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Keybindings', W: 1900, H: 1234, page: 'changes', overlay: 'keys' }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Agent settings', W: 1900, H: 1234, page: 'changes', overlay: 'agent', agent: { agent: 'claude', model: 'opus', effort: 'high', hover: 1 } }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Merge in progress', W: 1900, H: 1234, page: 'changes', merging: true, message: "Merge branch 'feature/askpass'", rows: S.FILES.map((f, i) => i === 2 ? { ...f, st: 'C', name: 'Toolbar.cpp' } : f) }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Diff hidden', W: 1900, H: 1234, page: 'changes', diffHidden: true }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Changes · table view', W: 1900, H: 1234, page: 'changes', filesView: 'table' }), 'Screens · Wide');
emit(S.screen({ id: 'Wide · Changes · compact view', W: 1900, H: 1234, page: 'changes', filesView: 'compact' }), 'Screens · Wide');
// Half 945x1234 & quarter 945x612
emit(S.screen({ id: 'Half · Changes', W: 945, H: 1234, page: 'changes' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Half · History', W: 945, H: 1234, page: 'history' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Half · Mini rail', W: 945, H: 1234, page: 'changes', mini: true }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Half · Mini rail · commit popover', W: 945, H: 1234, page: 'changes', mini: true, overlay: 'commit', message: 'Make the toolbar tiling aware', messageBody: ['', '- Fold labels, then icons, then buttons'] }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Half · Changes · table view', W: 945, H: 1234, page: 'changes', filesView: 'table' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Half · Repo menu', W: 945, H: 1234, page: 'changes', overlay: 'repo' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Half · Agent settings · Codex', W: 945, H: 1234, page: 'changes', overlay: 'agent', agent: { agent: 'codex', model: 'gpt-6-astra', effort: '', otherOpen: true, otherValue: 'gpt-5.5-codex' } }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Quarter · Changes', W: 945, H: 612, page: 'changes' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Quarter · History', W: 945, H: 612, page: 'history' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Quarter · More menu', W: 945, H: 612, page: 'changes', overlay: 'more' }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Shallow · Changes 945x400', W: 945, H: 400, page: 'changes' }), 'Screens · Half & Quarter');
// Narrow: third 627x612, eighth 470x612
emit(S.screen({ id: 'Third · Changes tab', W: 627, H: 612, page: 'changes' }), 'Screens · Narrow');
emit(S.screen({ id: 'Third · Diff tab', W: 627, H: 612, page: 'diff' }), 'Screens · Narrow');
emit(S.screen({ id: 'Third · History tab', W: 627, H: 612, page: 'history' }), 'Screens · Narrow');
emit(S.screen({ id: 'Third · Sync menu', W: 627, H: 612, page: 'changes', overlay: 'sync' }), 'Screens · Narrow');
emit(S.screen({ id: 'Third · Options menu', W: 627, H: 612, page: 'changes', overlay: 'options' }), 'Screens · Narrow');
emit(S.screen({ id: 'Third · Merge dialog', W: 627, H: 612, page: 'changes', overlay: 'merge' }), 'Screens · Narrow');
emit(S.screen({ id: 'Third · Agent settings · none installed', W: 627, H: 612, page: 'changes', overlay: 'agent', agent: { agent: 'none' } }), 'Screens · Narrow');
emit(S.screen({ id: 'Third tall · Changes 627x1234', W: 627, H: 1234, page: 'changes' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · Changes tab', W: 470, H: 612, page: 'changes' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · Diff tab', W: 470, H: 612, page: 'diff' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · Changes tab · table view', W: 470, H: 612, page: 'changes', filesView: 'table' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · History tab', W: 470, H: 612, page: 'history' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · Branch menu', W: 470, H: 612, page: 'changes', overlay: 'branch' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth shallow · 470x400', W: 470, H: 400, page: 'changes' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · Merge dialog', W: 470, H: 612, page: 'changes', overlay: 'merge' }), 'Screens · Narrow');
emit(S.screen({ id: 'Eighth · Agent settings', W: 470, H: 612, page: 'changes', overlay: 'agent', agent: { agent: 'claude', model: '', effort: '' } }), 'Screens · Narrow');
// Light theme
K.setTheme('light');
emit(S.screen({ id: 'Light · Wide Changes', W: 1900, H: 1234, page: 'changes' }), 'Light theme');
emit(S.screen({ id: 'Light · Quarter History', W: 945, H: 612, page: 'history' }), 'Light theme');
emit(S.screen({ id: 'Light · Third Diff tab', W: 627, H: 612, page: 'diff' }), 'Light theme');
K.setTheme('dark');
// standalone patterns for the Components page
{ const c = new K.Canvas('ChangesTree', 560, 300); S.changesTable(c, 0, 0, 560, 300, 'xl', { filesView: 'tree' }); emit(c, 'Components'); }
{ const c = new K.Canvas('CommitPopover', 360, 1); const h = S.commitPopover(c, { x: 0, y: 0, w: 360 }); c.h = h; emit(c, 'Components'); }
{ const c = new K.Canvas('AgentPopover', 360, 1); const h = S.agentPopover(c, { x: 0, y: 0, w: 360, agent: 'claude', model: 'opus', effort: 'high', hover: 1 }); c.h = h; emit(c, 'Components'); }
fs.writeFileSync(OUT + '/frames.json', JSON.stringify(frames, null, 1));
console.log(frames.length + ' frames');
const SH = require('./sheets.js');
K.setTheme('dark');
emit(SH.cover(), 'Cover'); emit(SH.foundations(), 'Foundations'); emit(SH.components(), 'Components'); emit(SH.layoutRules(), 'Layout rules');
// Proposals (2026-09-25): going past the history search's 10,000-match cap.
emit(SH.searchProposals(), 'Screens · Half & Quarter');
[['A', 'Count-row action'], ['B', 'Filter-row toggle'], ['C', 'In-field toggle'], ['D', 'Search options menu']].forEach(([p, name]) => {
  const search = on => ({ value: 'fix', proposal: p, on });
  const menu = p === 'D' ? { overlay: 'searchOptions' } : {};
  emit(S.screen({ id: `${p} · ${name} · Half · capped`, W: 945, H: 1234, page: 'history', search: { ...search(false), menu: p === 'D' }, ...menu }), 'Screens · Half & Quarter');
  emit(S.screen({ id: `${p} · ${name} · Half · full search on`, W: 945, H: 1234, page: 'history', search: search(true) }), 'Screens · Half & Quarter');
  emit(S.screen({ id: `${p} · ${name} · Eighth · capped`, W: 470, H: 612, page: 'history', search: { ...search(false), menu: p === 'D' }, ...menu }), 'Screens · Half & Quarter');
});
// Extra narrow (2026-09-25): the stacked bar cannot hold the view toggle (≈ < 420), so it takes its own row.
// Appended here so the numbering of the frames above stays put.
emit(S.screen({ id: 'Extra narrow · Changes tab', W: 340, H: 612, page: 'changes' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · Diff tab', W: 340, H: 612, page: 'diff' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · History tab', W: 340, H: 612, page: 'history' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · Branch menu', W: 340, H: 612, page: 'changes', overlay: 'branch' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · Sync menu', W: 340, H: 612, page: 'changes', overlay: 'sync' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow shallow · 340x493', W: 340, H: 493, page: 'changes' }), 'Screens · Narrow');
// New branch flow (2026-09-25): the branch menu's New branch… row (Ctrl+N), the card, History's way in.
// In Figma: the section "Flow · New branch" at the bottom of Screens · Half & Quarter.
{
  const Q = { W: 945, H: 612, page: 'changes' }, P = 'Screens · Half & Quarter', name = 'feature/tile-rules';
  emit(SH.newBranchFlow(), P);
  emit(S.screen({ id: 'New branch · 1 Branch menu', ...Q, overlay: 'branch', hover: 'new' }), P);
  emit(S.screen({ id: 'New branch · 2 Name typed', ...Q, overlay: 'branch', query: name }), P);
  emit(S.screen({ id: 'New branch · 3 Card', ...Q, overlay: 'newBranch', newBranch: { name } }), P);
  emit(S.screen({ id: 'New branch · 4 Pick where it starts', ...Q, overlay: 'newBranch', newBranch: { name, pickerOpen: true } }), P);
  emit(S.screen({ id: 'New branch · 5 Name taken', ...Q, overlay: 'newBranch', newBranch: { name: 'feature/askpass', taken: true } }), P);
  emit(S.screen({ id: 'New branch · 6 Created', ...Q, branch: name, noUpstream: true, status: 'Created ' + name + ' at d444446 and switched to it' }), P);
  emit(S.screen({ id: 'New branch · 7 History · commit menu', ...Q, page: 'history', commit: 2, overlay: 'commitMenu' }), P);
  emit(S.screen({ id: 'New branch · 8 History · changes in the way', ...Q, page: 'history', commit: 2, overlay: 'newBranch', newBranch: { name: 'hotfix/clone-states',
    base: { kind: 'commit', name: '14d6d14', sha: '14d6d14', msg: 'Add repository cloning with GitHub browser integration' },
    blocked: { title: '2 changed files differ at 14d6d14', lines: ['GitRepo.cpp, Toolbar.cpp', 'Commit them first to switch to it.'] } } }), P);
  emit(S.screen({ id: 'New branch · Eighth · Card', ...Q, W: 470, overlay: 'newBranch', newBranch: { name } }), P);
  emit(S.screen({ id: 'New branch · Extra narrow · Card', ...Q, W: 340, overlay: 'newBranch', newBranch: { name } }), P);
}
// Folded header rows (2026-09-26): on a two-row bar and in a shallow window the CHANGES and MESSAGE rows
// give way, their controls lead More (Files view ›, Show unversioned files, Agent settings…); on two rows
// the sync dropdown is a borderless miniature; the options menu keeps Amend, without the unversioned toggle.
emit(S.screen({ id: 'Extra narrow · More menu', W: 340, H: 612, page: 'changes', overlay: 'more' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · Files view submenu', W: 340, H: 612, page: 'changes', overlay: 'filesView' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · Options menu', W: 340, H: 612, page: 'changes', overlay: 'options' }), 'Screens · Narrow');
emit(S.screen({ id: 'Extra narrow · Agent settings', W: 340, H: 612, page: 'changes', overlay: 'agent', agent: { agent: 'claude', model: '', effort: '' } }), 'Screens · Narrow');
emit(S.screen({ id: 'Shallow · More menu 945x400', W: 945, H: 400, page: 'changes', overlay: 'more' }), 'Screens · Half & Quarter');
// Settings (2026-09-26): the footer's cog (left of the keys button), More → Settings…, Ctrl+,.
emit(S.screen({ id: 'Quarter · Settings', W: 945, H: 612, page: 'changes', overlay: 'settings', settings: {} }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Quarter · Settings · restart Nautilus', W: 945, H: 612, page: 'changes', overlay: 'settings', settings: { checked: true, restart: true } }), 'Screens · Half & Quarter');
emit(S.screen({ id: 'Extra narrow · Settings', W: 340, H: 612, page: 'changes', overlay: 'settings', settings: { checked: true } }), 'Screens · Narrow');
fs.writeFileSync(OUT + '/frames.json', JSON.stringify(frames, null, 1));
fs.writeFileSync(OUT + '/manifest.json', JSON.stringify(manifest));
console.log(frames.length + ' frames total');
// Icon glyphs for the Figma "Icon" component set (see figma/README).
fs.writeFileSync(OUT + '/icons.json', JSON.stringify(Object.fromEntries(Object.entries(K.ICON).map(([k, v]) => [k, require('@mdi/js')[v]]))));
