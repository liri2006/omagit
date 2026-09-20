# Omagit Figma design generator

Generates the design frames for the Omagit Figma file
(https://www.figma.com/design/aCJ6eG0PoX47UuC2OOim9v/Omagit) as SVG.

    npm install
    node build.js          # writes out/*.svg and out/frames.json

- `kit.js` — theme palettes (Moodpeak dark/light), type scale, SVG primitives and
  the widget builders (buttons, fields, chips, tables, diff rows, menus, dialogs).
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
