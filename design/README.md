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
| 4 | Files-view switcher | done |
| 5 | Mini rail commit tile and popover | done |
| 6 | Agent settings popover | done |
| 7 | Stacked layouts | done |
| 8 | Diff pane toolbar | done |

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
  CHANGES row, the active one selected; the choice is `window/filesView`.
- Tree (`src/ChangesTreeModel.*`): directories first and alphabetically, 14 px
  per level, chevron and folder icon, files under their parent's folder icon,
  collapsed directories show `N files`; keyboard navigation and check state per
  directory. Three columns — a fixed 30 px checkbox, Name, a 30 px `St` pill —
  with all the depth painted in Name, so the checkbox gutter is one straight
  line (indentation 0, no root decoration).
- Compact is the same table with its other columns hidden, Name filling the
  width and Status a 30 px pill column; `St` is painted over the section by the
  header alone, so the shared model and proxy keep their own labels. Going back
  to the table restores the user's widths, the resize modes and the delegates,
  and Path absorbs the viewport again.
- The flat proxy and the table's selection model stay canonical in all three:
  the tree is an adapter over the proxy, a tree file becoming current selects
  the flat row (which is what drives the diff), and a directory leaves the
  current file alone. Switching reveals the current file and announces nothing.
- A directory's box acts on exactly the proxy-visible files under it, folded
  branches included — `setPathsChecked()` grew a current-paths-only mode for
  that and for the check-all box, so a rename source that spells another shown
  file's path cannot drag it in. Amending keeps the old matching on purpose.
- Which directories are folded is the session's, by exact path, reapplied
  synchronously as the tree is rebuilt (before MainWindow restores the scroll);
  nothing of it is written to the settings.
- `--files-view compact|tree|table` is the deterministic startup override for
  screenshots and tests: validated before anything is opened (exit 2 otherwise)
  and never written back.

Not in this phase: the design's other column changes (no `Ext`, one `+ −`
column) and the tree's `+ −` column, both still deferred; the width-driven
compact default and the stacked layouts belong to phase 7, the Mini commit
tile to 5 and the agent popover to 6. HistoryView stays a plain table.

### 5. Mini rail commit tile and popover

Fixed a functional hole: Mini was a review layout and could not commit.

- The rail is `space(52)` wide now. Under Refresh, 8 px, a hairline, 8 px, then
  the commit tile (`src/MiniRail.*`): one widget as wide as the rail and a
  badge's rise taller than its 40 px accent square (fill 0.08, 0.18 on hover
  or while the card is open, 0.22 pressed), so the circular badge of checked
  files — the miniatures' own badge and painter, unscaled — overhangs the
  square's corner inside it. The badge reads the check marks of whatever model
  the rail shows, on its five structural notifications, and nothing else.
  Commit view only; Ctrl+click on a miniature now also focuses the list.
- `src/CommitPopover.*`: a 360 px card (`QFrame#commitPopover`, 2 px accent
  frame) inside the window, 8 px right of the rail, its bottom on the tile's
  bottom, clamped by the window's right margin. MESSAGE with the cog, the
  message box (the page's own `QTextDocument`: one text, one undo stack, the
  agent streaming into both), the regular 11 px `n / m files selected · Space
  on a tile toggles it` hint (`%small%`, elided), a hairline, Amend and the
  default Commit. The editor starts exactly `space(32)` under the card's top
  and `space(10)` in from its left, grows with its text to a third of the
  window, and the card grows upwards.
- The page stays the owner: `commitControls()` is one snapshot of what its
  buttons say (re-read on `commitControlsChanged()`), and the card's buttons
  call its `commit()` (now telling whether it committed), `generateMessage()`,
  `setAmendChecked()` and `showAgentMenuAt(anchor)`.
- Ctrl+Enter is the window's (listed once, *Commit view, Mini rail*; the
  keypad's Enter unlisted): commit from the open card, else open it in Mini,
  else press the page's button. Escape, the tile again, a press elsewhere in
  the window, a commit, the history, Docked and another repository close it;
  the rail, the wheel, Refresh, F5 and other windows do not.
  `--mini --screenshot-menu commit` pictures it.
- A shared document wraps at the width of one box only: `MessageEdit` hands
  the wrapping to the box on screen whenever it is shown or re-themed.

Not in this phase: the agent settings popover (6), the stacked layouts and the
Diff tab's rail (7), a miniature redesign, and any change to the Docked commit
page's layout or to the history.

### 6. Agent settings popover

The cog's `TickMenu` and its `QInputDialog` for "Other…" gave way to the
design's card.

- `src/Segmented.*`: the top bar's private tab classes, extracted as
  `SegmentButton` / `SegmentStrip` for N segments with a stretch mode
  (`floor(w / n)` each, the last taking the remainder, content centred as
  `kit.js` rounds it). The top bar uses them unchanged: Docked and History
  screenshots at base 12 and 16 are pixel-identical to the build before.
- `src/AgentPopover.*`: `QFrame#agentPopover`, an overlay of the central
  widget like the commit card (2 px accent frame shared in the stylesheet),
  `min(space(360), inner width)` wide, its right edge on the cog's, 6 px under
  it, moved up in a short window; opened from the Mini commit card's cog it sits
  8 px beside that card instead (`setBeside()`), when at least 240 px of room is
  left there. AGENT is a stretched strip of the installed
  agents with the Omarchy default named in a caption note; MODEL painted rows
  (name, the CLI id in the 11 px small text ending 30 px in, the accent tick),
  Default first and a typed-in model ahead of the catalog; `Other model…` a
  ghost text button that turns into the regular field (Return saves, Escape
  closes the field before the card); REASONING a painted level track (Default
  plus the model's levels, click or Left/Right), left out when the model has
  none; a hairline and the default `Generate now  Ctrl+G`. Nothing installed:
  the robot, the headline, INSTALL ONE and two `QFrame#commandRow`s whose copy
  buttons put the bare command on the clipboard.
- The page stays the owner: `CommitPage::applyAgentChoice()` saves and
  refreshes the generate button, both cogs emit
  `agentSettingsRequested(anchor)`, and the window toggles the card at that
  anchor (`--screenshot-menu agent` in Docked and Mini). Every choice is saved
  at once and the card is rebuilt from `savedChoice()`, the keyboard kept on
  the part that had it; no new settings key. The card takes the keyboard as
  it opens and gives it back as it closes.
- Dismissal: Escape, the cog again, a press elsewhere in the window, Generate
  now, the cog hiding (the commit card closing, History, Docked/Mini) and
  another repository. The commit card treats a press on the agent card as its
  own (`CommitPopover::setCompanion()`).

Not in this phase: keyboard focus for the agent picker's segments (they stay
pointer-only, like the top bar's tabs), the design's 0.3-alpha track line (the
theme's hairline is used), and anything about the agents themselves.

### 7. Stacked layouts

The tiling end of the design (the `s` / `xs` levels of `screens.js`).

- Under `space(700)` of window width the body stacks, as a presentation only:
  `MainWindow` keeps the Docked/Mini and diff-pane preferences (and the left
  width) and derives what is shown from them and a temporary Diff-tab flag.
  Changes and History fill the body; Diff is the existing Mini rail beside the
  diff pane, on the mode's files. No widget is reparented and no model reset:
  stacking, unstacking and tab switches keep the selection, the scroll offsets
  and the place in the diff. The first classification is the first show's
  resize event, after every startup flag; unstacking into Docked puts the
  left width back (`applySplitterSizes()`, factored out of the first show).
- Tabs, not modes: `TopBar::Tab` (Changes, Diff, History) with
  `setCurrentTab()` / `tabRequested()`. Changes and History (and Ctrl+1/2)
  change the mode only when it is another one; Diff, Ctrl+B and Ctrl+Shift+B
  (stacked) and a double-click on a file are the Diff tab, saving nothing.
  The commit card and the agent card follow the rail: on the Diff tab in the
  commit view, closed on leaving it or unstacking.
- `SegmentStrip::setSegmentVisible()`: the Diff segment takes part only while
  stacked; hint, placement and dividers count the segments not hidden.
- The top bar's three stacked levels (tab labels, glyphs, the branch eliding
  to its 72 px), the bare folder, and on the right a 92 px **sync dropdown**
  (`↓n ↑n ⌄` painted inline, walking dots while one side is busy, 99+ for
  three digits, Merge's mark in its corner) and More, which is always there
  and keeps its one width. The layout toggles hide. Its menu: Pull, Push,
  Fetch, Merge…, each the button's own click.
- More also carries Refresh, Open repository…, Clone… and Keybindings after
  the folded sync buttons, on every row.
- The stacked action bar: the options `…` (Check all / Uncheck all, Show
  unversioned files, Amend last commit, Generate message / Stop generating,
  each the page's own path) and Commit across the rest of the row, without
  the `⏎` (the card's keeps it).
- While `window/filesView` is unset (and no `--files-view`), the list is
  compact stacked and the table otherwise, never saved.
- A drop-down of the right group or of the action bar stays inside the
  window (`ui::keepMenuInWindow()`).
- `--screenshot-menu sync|more|options`.

Not in this phase: History's icon-only All branches control, its folded files
button and the details date; the footer's `xs` status text; the design's
height classes and shallow windows; the forced unified view of the Diff tab
(the compact diff toolbar is phase 8); the action bar's Abort button and hint; right-aligned shortcut hints in
menus (tooltips carry them); repository search; a MiniRail redesign.

Not in any phase yet: a search prompt in the repo menu (it exists in Figma,
but is new functionality in the app).

### 8. Diff pane toolbar

Phase 7's review, finding 4: the diff pane's row (Prev, Next, the counter,
Two-pane, Whitespace, Syntax) elided every label mid-word in a narrow pane,
which the stacked Diff tab made the normal look of a 470 px window.

- Three forms, picked by the pane's own width at fixed thresholds that scale
  with the text size (`screens.js diffPane()`): labelled from `space(900)`;
  the three view options as glyph squares from `space(560)`; below that Prev
  and Next as glyph squares too, the counter as `n/m` without the summary,
  and the options behind a `…` (`View options`, a `TickMenu` of three
  checkable entries filled on `aboutToShow`, each the button's own click).
- One button per action in every form: a form change swaps the text and the
  width, so the connections, the checked states and the window's Ctrl+T /
  Ctrl+W / Ctrl+L on a hidden button carry on. `ui::setIconForm()` sets the
  `iconForm` property (`padding: 5px 0`, so both forms are one height) and the
  28 px square, repolishing only on a real change; a resize within a form
  touches nothing, and `applyTheme()` re-applies the form for a new text size.
- The design's gaps: 4 between Prev and Next and between the options, 10 on
  either side of the counter.
- `--screenshot-menu diff` opens the `…` menu where the button is shown.

Not in this phase: the design's `Split ▾ / Unified ▾` dropdown in place of
the Two-pane toggle, the forced unified view of the stacked Diff tab, and the
coloured summary (`Modified +4 −2`).

### Gap fixes (after phase 9)

Done 2026-09-23 (brief `docs/briefs/2026-09-23-design-rework-gaps.md`,
record `docs/reviews/2026-09-23-design-rework-gaps.md`):

- The sync buttons' badge is the design's square hanging over the top-right
  corner (`space(4)` past the right edge, `space(5)` above the top, kit.js
  `badge()`), painted by a `BadgeLayer` over the whole top bar, because a
  widget cannot paint outside its own rect; the bar keeps `space(5)` above
  its row for it. No reserve inside the buttons any more: the icon form is
  `ui::setIconForm()`'s 28 px square, the glyph centred by its ink (a Nerd
  Font glyph's ink is wider than its advance).
- The merge dialog is `min(640, window − 24)` wide and stacks its pickers
  under 520 (`fitWidth()` / `arrangePickers()`).
- The footer's status is a `ui::ElidedLabel` (the design's shorter `xs`
  text, done by eliding).
- No header text over the history graph column.

### History page

Done 2026-09-24 (brief `docs/briefs/2026-09-24-history-view-design.md`,
record `docs/reviews/2026-09-24-history-view-design.md`): `historyPage()`,
`commitsTable()` and `commitDetails()` in the app.

- The filter row: the field's magnifier is its own glyph and stays while
  typing, the placeholder says as much as the field's width holds, then
  All branches 8 px on and Refresh 6 px on.
- The commit list's columns follow the window's width class (Graph 40 /
  Message / Author 90 / Date 130, then 100, then Graph 36 / Date 90, then
  Graph 30 / Message alone), no SHA column, no sideways scrolling; author
  and date small and dim, the date without its time where the column
  cannot hold it.
- The graph: lanes 12 px apart, the first 14 / 12 / 9 px in, accent then
  magenta, 2 px lines, every node the same 4 px disc with a background
  ring. **The one deliberate difference from Figma, the user's:** the
  lanes keep the app's connections — curves into and out of a lane where
  a branch forks or merges, lines that start and end at the nodes —
  where the frames draw straight lanes without any.
- Ref chips (`src/RefChip.*`, shared with the card): the checked-out
  branch solid accent, other branches accent, remotes magenta, tags
  yellow; the xs width leaves the remotes out of the rows.
- `src/CommitDetails.*`: the card in place of the key/value text — bold
  title, accent short SHA, author and date, parents with the chips, the
  body in 17 px lines (selectable, scrolling), a copy button for the full
  SHA; stacked, `N files ›` opens the Diff tab.
- The commit's files: row numbers (the frames' checkboxes are the kit's
  artefact — a commit's files cannot be checked), Name stretching, then
  Path / Status / `+ −` / Size by class (the `St` pill at m).
- Heights 150 / 110 (card 132 stacked) in a splitter whose handles are
  the 10 px gaps, re-applied until the user drags one; shallow windows
  show the commit list alone; the count row is the page's last 22 px.

## How a phase is done

1. A brief from the Figma frames and the generator's numbers; co-written with
   Codex for the non-trivial phases (3 to 5).
2. Implementation by an Opus agent, one at a time — the phases share
   `UiHelpers` and the stylesheet, so parallel agents would collide.
3. Review of the diff and of offscreen screenshots (fresh `XDG_CONFIG_HOME`
   per batch, base sizes 12 and 16), then the joint Codex review loop.
4. `tests/run.sh` green, then one commit.
