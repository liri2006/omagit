# OmaGit

A small classic *commit dialog* and *history viewer* for Linux, written in
C++/Qt 6 and themed by [Omarchy](https://omarchy.org). It shows the pending changes of
a git repository with a side-by-side diff for the selected file, the commit
history with a branch graph, and plugs into the Nautilus context menu as
**Open in OmaGit** (only shown inside git repositories).

## Features

- Changes list with checkboxes, path / extension / size / status / lines added / lines removed,
  coloured by status like a classic git GUI (modified, added, deleted, renamed, conflicted, unversioned).
- Diff view modelled on a classic diff tool: **two-pane side-by-side view by default** (HEAD on
  the left, working tree on the right, aligned row by row with grey filler where one side has
  nothing) and a one-pane unified view (toggle with the *Two-pane* button or Ctrl+T; the choice
  is remembered). Both show the whole file as context, orange removed lines, yellow added lines,
  inline (word-level) change highlighting, a margin with +/− icons and line numbers,
  next/previous change navigation (F8 / Shift+F8), optional whitespace markers, text selection
  and copy, and Ctrl+wheel zoom.
- Commit the checked files with a message (Ctrl+Enter).
- **Pull, Push and Fetch** in the toolbar above the left section (Ctrl+Shift+L / P / F), in
  both modes and on the Mini rail. The Pull button carries a badge with the number of commits waiting on the
  upstream, the Push button the number not pushed yet; a walking-dots badge shows while
  the count is being refreshed and a new number pops in when it changes. To keep the Pull
  count current OmaGit fetches by itself: shortly after start, every 3 minutes while the
  window is open, and when the window comes back to the front after a while — a cheap
  `git fetch --all` whose failures back off up to 15 minutes, so an offline machine is
  left alone (`remote/autoFetchSeconds` in `~/.config/omagit/omagit.conf`, 0 turns it off,
  as does `--no-fetch`). Fetches and pushes made in a terminal are picked up through a
  watch on the git directory. Push on a branch without an upstream publishes it
  (`git push -u`); hovering the branch name says which upstream the counts refer to.
- **Amend last commit**: tick the box (or start with `--amend`) and the message box gets the
  last commit's message while the changes list is compared against the commit before it, so
  the files of the last commit appear checked next to your new changes. *Amend* then rewrites
  HEAD with exactly the checked files (same parents and author); files of the old commit you
  untick go back to the working tree as staged changes. You are warned when the commit is
  already on a remote branch.
- **History** (Ctrl+2, or `--history`): the commits of the current branch (or *All branches*)
  with a lane graph, branch/remote/tag chips, author, date and SHA; filter by message, author
  or SHA (Ctrl+F); commits load 500 at a time as you scroll. Selecting a commit shows its
  details and the files it touched; selecting a file shows the diff against the parent in
  the same diff pane. Right-click a commit to copy its SHA or message.
- The toolbar adapts to the width of the left section: labels give way to icons, and icons
  to a *more* menu (Fetch folds first, Pull last), so the section can be dragged as narrow
  as you like; the diff pane can be dragged just as narrow. The toolbar's first button is
  the Docked/Mini toggle; Refresh is the small icon next to the *n / m selected* count (and
  next to *All branches* in History), F5 works everywhere.
- **Docked / Mini** layouts (the toolbar's first button or Ctrl+B; the choice is
  remembered). *Docked* keeps the left section (commit dialog or history) next to the diff
  pane. *Mini* shrinks it to a narrow rail of file miniatures (extension tile, status letter,
  dimmed when not part of the commit) so the diff gets the whole window; hovering a miniature
  pops up its name and folder right away, Space or Ctrl+click toggles it for the commit.
  The button in the window's top right corner (Ctrl+Shift+B) hides the diff pane so the
  left section fills the window; double-clicking a file brings it back for that file
  (with the pane showing, a double-click opens the file in its own program instead).
  Right-clicking a file offers *Open with …*, naming the program the desktop would use
  for it (from `xdg-mime`).
- Looks like an Omarchy system program: the UI follows the shell's control kit (square
  corners, one flat background, 1px hairline borders and foreground-alpha fills for
  normal/hover/selected states, accent-coloured selection, Nerd Font glyph icons, the shell's
  monospace type scale rooted at `[font] base-size` from `shell.toml`).
- Picks up the active Omarchy theme from `~/.local/state/omarchy/current/theme/colors.toml`
  (palette, light/dark mode, icon theme, monospace font) plus `shell.toml` (font size), and
  re-themes itself live when you run `omarchy theme set …` or change the font size.
- Refreshes when the working tree or index changes.

## Build & install

Requires `qt6-base` and `git`; the context menu needs `nautilus-python`.

```bash
./install.sh
```

This builds with `qmake6`, installs `~/.local/bin/omagit`, a desktop entry, an icon and
`~/.local/share/nautilus-python/extensions/omagit.py`, then restarts Nautilus so the menu
appears. `./uninstall.sh` removes everything again.

## Usage

```bash
omagit [path]            # any directory or file inside a repository (default: cwd)
```

`--history` opens the history view, `--amend` starts with *Amend last commit* ticked,
`--mini` starts in the Mini layout, `--full` with the diff pane hidden, and `--no-fetch` leaves the network alone.

Handy flags for development: `--select <repo-relative path>` pre-selects a file,
`--screenshot out.png` renders the window and exits (works with `QT_QPA_PLATFORM=offscreen`;
`--screenshot-after <ms>` sets the delay, e.g. to catch a running fetch),
and `OMAGIT_THEME_DIR=/usr/share/omarchy/themes/tokyo-night` previews another theme.

The git wrapper has a self-contained test program that builds throw-away repositories:

```bash
cd tests && qmake6 tests.pro && make && ../build/tests/gitrepo_test
```

## Layout

| File | Purpose |
|------|---------|
| `src/OmarchyTheme.*` | Parses `colors.toml`, builds the palette/stylesheet, watches for theme switches |
| `src/GitRepo.*` | Thin wrapper over the `git` CLI: status, diff, commit, amend, log, refs, upstream state, async runs |
| `src/RemoteSync.*` | Fetch / pull / push, the ahead/behind counts, automatic fetching with backoff, git-dir watch |
| `src/BadgeButton.*` | Tool button with a count badge, busy dots and a pop-in animation |
| `src/Toolbar.*` | Width-adaptive button row: labels → icons → "more" menu |
| `src/DiffModel.*` | Unified-diff parser and inline (token LCS) diff |
| `src/DiffView.*` | Custom-painted side-by-side viewer |
| `src/ChangesModel.*` | Table model for the changes list (also the files of a commit) |
| `src/HistoryModel.*` | Commit list model with incremental lane-graph layout |
| `src/HistoryView.*` | History view: filter, commit table with graph and ref chips, details, files |
| `src/MainWindow.*` | Window shell: modes and sync buttons, Docked/Mini layouts, diff pane and its toggle |
| `src/MiniRail.*` | The Mini layout's rail: file miniatures with instant path tooltips |
| `src/PaneLayout.h` | The Docked/Mini enum with its glyphs, names and settings keys |
| `tests/gitrepo_test.cpp` | Checks for status, amend, history and fetch/pull/push against scratch repositories |
| `nautilus/omagit.py` | Nautilus "Open in OmaGit" menu provider |
