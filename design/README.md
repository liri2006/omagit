# Design rework

The tiling-aware redesign lives in Figma
([Omagit](https://www.figma.com/design/aCJ6eG0PoX47UuC2OOim9v/Omagit)) and is
generated from [`figma-gen/`](figma-gen/README.md): `kit.js` holds the
controls, `screens.js` the layouts and their metrics. Those numbers are the
source of truth for the Qt app — read them there rather than measuring Figma.

All design pixels are meant for a 12 px base font. The app follows the Omarchy
text size, so every one of them goes through `ui::space(px)` and is re-applied
on `OmarchyTheme::changed`.

## Bringing it into the app

The redesign is a set of separate decisions that touch different parts of the
app, so it goes in by phases, one commit each. Every phase leaves the app
looking intentional on its own, keeps the deterministic screenshots
comparable, and can be checked frame by frame against Figma at the matching
tile widths (Wide, Half, Third, Eighth).

| # | Phase | Status |
|---|-------|--------|
| 1 | Kit primitives | done (`dc5abfe`) |
| 2 | Commit page content | done |
| 3 | Top bar restructure | done |
| 4 | Files-view switcher | |
| 5 | Mini rail commit tile and popover | |
| 6 | Agent settings popover | |
| 7 | Stacked layouts | |

### 1. Kit primitives

No layout change; what the later phases build on, in `src/UiHelpers.*`.

- `ui::space(px)`, and the section grid `headerRowHeight()` / `headerGap()` /
  `sectionGap()` (24 / 6 / 16) with `ui::sectionHeaderRow(label)`.
- `ui::iconButton()`: `Inline` is a 24 px ghost square for section header
  rows, `Toolbar` is 28 px wide and as tall as the row it sits in.
- `ui::promptField()` / `ui::promptBox()`: the search prompt of a popup wears
  the Omarchy menu look — no box, the popup's accent frame is the focus cue, a
  dim magnifier that stays while typing, a hairline underneath. Inline fields
  (history filter, clone dialog) keep their border, because the focus can be
  elsewhere.

### 2. Commit page content

All inside `CommitPage`; mostly moving and relabelling what exists.

- The count folds into the section title (`CHANGES · 5/7`) and the button
  reads `Commit 5 files` (Amend and Commit merge keep their wording).
- Check-all is a label-less checkbox in the table's header cell, partial when
  some files are checked.
- `Amend last commit` is a checkbox at the left of the bottom action bar.
- `Show unversioned files` is an eye icon button (selected when on) at the
  right of the CHANGES row, Refresh after it behind a 1 px divider.
- Generate leaves the action bar; the sparkle in the message box is the only
  trigger.
- Hidden files are never checked: turning the eye off unticks the unversioned
  files, and the check-all box, the title and the button all count the rows
  the list shows.

Not part of this phase: the design's other column changes (no `Ext`, one
`+ −` column).

### 3. Top bar restructure

The riskiest one: it changed the window skeleton and the width folding.

- `src/TopBar.*` is one row above the whole body, in every layout: the
  repository and branch chips, the `Changes n | History` tabs centred in the
  window like a window-level mode switch (clamped 16 px clear of both groups,
  nudged aside first, labels folding to glyphs second), the sync buttons with
  their more menu, and the two layout toggles. The left `Toolbar` is gone;
  DiffPane's navigation row stays where it is, less the diff toggle, which the
  top bar now owns for good.
- The tabs are painted `QToolButton` segments in one shared frame: the design's
  fills, a 14 px count pill on Changes, and plain accessible names.
- Seven folding levels, the first that fits winning: sync labels, sync buttons
  into the menu, the repository label, the tab labels, the last two sync
  buttons, and the branch name eliding only at the very end (72 px of it kept).
- Refresh sits in the CHANGES header and at the end of the history filter row.
- The repo chip is never hidden: label and chevron when there is room, a bare
  folder icon button at the narrow widths.
- The Mini rail keeps the miniatures, the commit hash and Refresh; its mode,
  sync and merge buttons were the top bar's twins and went with it.

Not in this phase: the stacked Diff tab and the combined sync dropdown (both
phase 7), the files-view switcher (4), the Mini commit tile and popover (5),
the agent popover (6), the action bar's options menu, further more-menu
commands, repository search, diff-nav restyling and renaming the keybindings
row.

### 4. Files-view switcher

The only phase with real new logic, independent of the others.

- Three ghost icon buttons — compact / tree / table — at the right of the
  CHANGES row, the active one selected.
- Tree: directories first, 14 px per level, chevron and folder icon, files
  under their parent's folder icon, collapsed directories show `N files`;
  keyboard navigation and check state per directory.

### 5. Mini rail commit tile and popover

Fixes a functional hole: Mini is a review layout and cannot commit today.

- A 40 px accent commit tile at the end of the rail, badge = checked files.
- A 360 px popover beside it: MESSAGE with the cog and sparkle, the
  `n / 7 files selected` hint, Amend last commit, a primary Commit. Reuses the
  commit page's parts, so it comes after phase 2.

### 6. Agent settings popover

Self-contained, and the current one works, so it is last.

- AGENT segmented control, MODEL rows with the CLI id and a tick, an
  `Other model…` text button that opens a field only on demand, REASONING as a
  level track, a full-width `Generate now` button.

### 7. Stacked layouts

The tiling end of the design (the `s` / `xs` levels of `screens.js`), left out
of phase 3 on purpose. Depends only on phase 3, so it can be pulled forward.

- Below the stacking width the splitter gives way to tabs: `Changes n | Diff |
  History` in the top bar, the Diff tab showing the Mini rail beside the diff;
  the layout toggles hide there.
- Pull and Push fold into one sync dropdown carrying both counts (`↓2 ↑1`), its
  menu listing Pull, Push, Fetch and Merge…; More gains Refresh, Open
  repository…, Clone… and Keybindings.
- The action bar's options `…` menu (Select all, Show unversioned files, Amend
  last commit, Generate message) and the full-width Commit button.
- Switching by window width, scaled with the text size like every other design
  pixel.

Not in any phase yet: a search prompt in the repo menu (it exists in Figma,
but is new functionality in the app).

## How a phase is done

1. A brief from the Figma frames and the generator's numbers; co-written with
   Codex for the non-trivial phases (3 to 5).
2. Implementation by an Opus agent, one at a time — the phases share
   `UiHelpers` and the stylesheet, so parallel agents would collide.
3. Review of the diff and of offscreen screenshots (fresh `XDG_CONFIG_HOME`
   per batch, base sizes 12 and 16), then the joint Codex review loop.
4. `tests/run.sh` green, then one commit.
