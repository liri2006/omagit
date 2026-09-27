# Omagit Figma design generator

Generates the design frames for the Omagit Figma file
(https://www.figma.com/design/aCJ6eG0PoX47UuC2OOim9v/Omagit) as SVG.

    npm install
    node build.js          # writes out/*.svg and out/frames.json

- `kit.js` — theme palettes (Moodpeak dark/light), type scale, the 4 px grid
  (`GRID`, the spacing scale `SP`, sizes `BOX`, padding `PAD`, gaps `GAP`,
  per-window `DENSITY`, `BAR`), SVG primitives and the widget builders
  (buttons, fields, chips, tables, diff rows, menus, dialogs). `icon()` takes a
  box: the glyph fills 7/8 of it (14 in 16).
- Every composer takes its numbers from those tokens; `screens.js`'s
  `density(W, H)` picks the side margins and pane gaps by width class and the
  block and section gaps by height class.
- `screens.js` — the full window at every width/height class (wide, half,
  quarter, third, eighth, shallow) and its overlays. The Changes | History
  view toggle (plus Diff when stacked) is centred in the top bar; Refresh sits
  in the CHANGES header row and at the end of the History filter row. Overlays
  are the menus, the merge dialog, the keys panel and the agent settings
  popover `agentPopover` with its Claude / Codex / none-installed states.
  The CHANGES title carries the checked / total count ("CHANGES · 5/7") and
  the Commit button says how many files it commits ("Commit 5 files"); right of
  the title: view switcher, divider, unversioned toggle; check-all lives in the
  table header cell and Amend in the action bar next to Commit.
  The changes list has three views (`filesView`: table, compact, lazygit-style
  tree via `fileTree`), switched by three icon buttons on the CHANGES row.
  Extra narrow (stacked, and the toggle's icons would touch the branch or the
  sync group, ≈ < 420): the toggle takes a full-width row of its own under the
  controls (an 80 px bar), labelled again, and the menus hang under the
  controls row, over it; the diff header drops its HEAD → Working tree title
  where it would touch the path.
  Folded header rows (2026-09-26): on a two-row bar and in a shallow window
  `changesPage` drops the CHANGES and MESSAGE rows (`folded`); their controls
  lead the More menu on the Changes tab (`overlay: 'more'`: Files view ›, Show
  unversioned files, Agent settings…; `overlay: 'filesView'` opens the
  submenu, placed by Qt's screen-edge rule with the window for the screen), a
  shallow unstacked bar keeps More at every width, the agent settings hang from
  More, and on two rows the sync dropdown is a borderless 64 px miniature
  (`SyncDropdown/mini`). The Options menu carries Amend last commit alone, as the
  app's does, and a menu hint gives way where it would touch its label.
  CHANGES is the top section and MESSAGE the bottom one, over the action bar;
  the agent popover hangs under its cog, or stands over it where the frame
  has no room below (`agentPopoverHeight`).
- Proposals (2026-09-25), for going past the history search's 10,000-match
  cap: `historyPage` takes `search: { value, proposal: 'A'…'D', on, menu }`
  (a filter's matches: no graph, Author in every class, `FIX_MATCHES`), the
  `searchOptions` overlay is proposal D's menu, and `searchProposals()` in
  `sheets.js` is the notes sheet. In Figma they sit in the section
  "Proposals · Full history search" at the bottom of Screens · Half &
  Quarter (the Starter plan has no room for a page of their own).
- New branch flow (2026-09-25): the branch menu ends with New branch…
  (Ctrl+N) after a separator, and a search no branch matches leaves
  New branch “…” as its only row (`overlay: 'branch'`, `query`, `hover:
  'new'`); `newBranchCard()` is the card (`overlay: 'newBranch'`, `newBranch:
  { name, taken, base, blocked, pickerOpen }`), hanging where the branch menu
  does; `overlay: 'commitMenu'` is a History commit's menu (`commit` picks
  the selected row). `topBar` takes `branch` and `noUpstream` (no sync
  badges). `newBranchFlow()` in `sheets.js` is the notes sheet. In Figma:
  the section "Flow · New branch (2026-09-25)" at the bottom of Screens ·
  Half & Quarter, with prototype clicks between the frames and two flows
  (New branch, New branch from History).
- Settings (2026-09-26, after the app): the footer carries a settings cog an
  item gap left of the keys button, More ends with Settings… (Ctrl+,), and
  `settingsDialog()` (`overlay: 'settings'`, `settings: { checked, restart }`)
  is the dialog: the dialog card's head, FILE MANAGER with the Nautilus
  checkbox, the note wrapped under its text (`wrapLines`) and, when a running
  Nautilus must restart, Restart Nautilus under the note; Close at the right.
  Frames 84–86: Quarter · Settings, … · restart Nautilus (under Quarter ·
  Changes / History on Screens · Half & Quarter) and Extra narrow · Settings.
- `sheets.js` — Cover, Foundations, Components and Layout-rules sheets.
- `build.js` — the frame list; `out/frames.json` records name, page and x offset.

Import into Figma: with the Figma editor open, the page exposes the plugin API
as `figma`; add a hidden `<input type=file multiple>` to the page, upload the
SVGs into it, then for each file `figma.createNodeFromSvg(await file.text())`,
name it after `frames.json`, set `x`, and `appendChild` it to its page.

## Components and instances (`figma/`)

`build.js` also writes `out/manifest.json` (every control placement per frame:
kind, rect and props, recorded by the builders in `kit.js`) and `out/icons.json`
(icon name → MDI path).

- `figma/lib.js` — helpers on `window.__om` (variables, styles, fonts, paints,
  retheming, file upload access).
- `figma/build-components.js` — rebuilds the control components on the
  Components page as auto-layout components with properties: `Button`
  (variant × state; Label, Icon swap, Show icon/label/chevron/badge/mark),
  `Chip/branch`, `Chip/repo`, `Badge`, `SyncDropdown`, `Icon` (one variant per
  glyph), `Segment`, `Segmented` (hug/stretch), `Field`, `Checkbox`, `RefChip`,
  `StatusPill`, `SectionLabel`, `IconButton` (variant ghost / default × state × size
  24 / 28, Icon swap; the normal variants carry hover / press prototype reactions).
  Re-runnable; the previous generation is removed.
- `figma/icon-buttons.js` — one-off migration (2026-09-18): adds the `IconButton`
  set to an existing file and swaps every icon-only `Button` instance for it, moved
  to the manifest rects (needs `manifest.json`).
- `figma/import-frames.js` — imports the frames of `out/` the file does not have
  yet (or `window.__omImport`), after the last frame of their page, first adding
  any icon of `icons.json` the Icon set lacks; sheets whose
  page is `Components` become components under the pattern rows. Sets
  `window.__omOnly` so `replace-instances.js` can follow.
- `figma/add-view-switcher.js` — one-off (2026-09-18): adds the files-view
  switcher (table / compact / tree IconButtons) to frames imported before it
  existed and moves the "5 / 7 selected" count left of it.
- `figma/fold-count.js` — one-off (2026-09-18): relabels every CHANGES section
  title to "CHANGES · 5/7" and every Commit button to "Commit 5 files" (hugging
  the right-aligned ones), and deletes the old "5 / 7 selected" texts.
- `figma/grid-rebuild.js` — one-off (2026-09-24): the move to the 4 px grid.
  The controls changed anatomy (16 px icon boxes, 8 px padding, 4 px gaps, 12 px
  badges, 16 px checkboxes), so it swapped the Components frame for a fresh
  import of the sheet (`__omGrid.prepare()`, then `build-components.js` and
  `replace-instances.js` on it), re-imported every frame in place keeping the
  layer order (`__omGrid.batch(ids)`) and deleted the old frame once nothing
  used its components (`__omGrid.finish()`).
- `figma/sync-mini.js` — one-off (2026-09-26): adds the `SyncDropdown/mini`
  component (the two-row bar's borderless miniature) under `SyncDropdown` on
  the Components sheet; `replace-instances.js` uses it for `mini` records.
- `figma/settings-cog.js` — one-off (2026-09-26): gives every screen's footer
  its settings cog, a copy of the keys IconButton 32 px to its left with the
  cog swapped in (Light frames keep their overrides); idempotent.
- `figma/replace-instances.js` — swaps the flat SVG groups in every frame for
  instances of those components, matched by layer name and position from the
  manifest; light-theme frames get their instances rebound to the Light styles.
  Set `window.__omOnly = ['Wide · Changes']` to limit the run.

Running them: add a hidden `<input type=file multiple id=__omfile>` to the Figma
editor page, upload `lib.js`, the script, `icons.json` and `manifest.json` into
it, then `eval(await file.text())` each in turn (lib first).

Pitfalls learned the hard way:
- Instances render variable-bound paints at 100 % whatever the paint's opacity
  says, so translucent chrome is a `fill` / `border` child rect with layer
  opacity, stretched over the auto-layout frame (as in the flat SVG version).
- A bound paint written to a node once snaps to opacity 1; writing the same
  paint twice keeps it (`om.setFills`). Components honour it, instances do not.
- `getLocal*Async` and `figma.variables.get*Async` hang in the page context;
  use the sync getters. The Starter plan has no room for a scratch page.
