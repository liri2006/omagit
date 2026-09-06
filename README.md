# OmaGit

A small classic *commit dialog* and *history viewer* for Linux, written in
C++/Qt 6 and themed by [Omarchy](https://omarchy.org). It shows the pending changes of
a git repository with a side-by-side diff for the selected file, the commit
history with a branch graph, and plugs into the Nautilus context menu as
**Open in OmaGit** (only shown inside git repositories).

## Features

- Changes list with checkboxes, path / extension / status / lines added / lines removed,
  coloured by status like a classic git GUI (modified, added, deleted, renamed, conflicted, unversioned).
- Diff view modelled on a classic diff tool: **two-pane side-by-side view by default** (HEAD on
  the left, working tree on the right, aligned row by row with grey filler where one side has
  nothing) and a one-pane unified view (toggle with the *Two-pane* button or Ctrl+T; the choice
  is remembered). Both show the whole file as context, orange removed lines, yellow added lines,
  inline (word-level) change highlighting, a margin with +/− icons and line numbers,
  next/previous change navigation (F8 / Shift+F8), optional whitespace markers, text selection
  and copy, and Ctrl+wheel zoom.
- Commit the checked files with a message (Ctrl+Enter).
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
- **Full / Sidebar** (Ctrl+B): let the left section (commit dialog or history) fill the whole
  window, or keep it as a sidebar next to the diff pane. The choice is remembered.
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

`--history` opens the history view, `--amend` starts with *Amend last commit* ticked and
`--full` starts with the left section filling the window.

Handy flags for development: `--select <repo-relative path>` pre-selects a file,
`--screenshot out.png` renders the window and exits (works with `QT_QPA_PLATFORM=offscreen`),
and `OMAGIT_THEME_DIR=/usr/share/omarchy/themes/tokyo-night` previews another theme.

The git wrapper has a self-contained test program that builds throw-away repositories:

```bash
cd tests && qmake6 tests.pro && make && ../build/tests/gitrepo_test
```

## Layout

| File | Purpose |
|------|---------|
| `src/OmarchyTheme.*` | Parses `colors.toml`, builds the palette/stylesheet, watches for theme switches |
| `src/GitRepo.*` | Thin wrapper over the `git` CLI: status, diff, commit, amend, log, refs |
| `src/DiffModel.*` | Unified-diff parser and inline (token LCS) diff |
| `src/DiffView.*` | Custom-painted side-by-side viewer |
| `src/ChangesModel.*` | Table model for the changes list (also the files of a commit) |
| `src/HistoryModel.*` | Commit list model with incremental lane-graph layout |
| `src/HistoryView.*` | History view: filter, commit table with graph and ref chips, details, files |
| `src/MainWindow.*` | Window shell: Commit/History modes, Full/Sidebar toggle, diff pane |
| `tests/gitrepo_test.cpp` | Checks for status, amend and history against scratch repositories |
| `nautilus/omagit.py` | Nautilus "Open in OmaGit" menu provider |
