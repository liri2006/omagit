# Omagit

A small classic *commit dialog* for Linux, written in C++/Qt 6 and themed
by [Omarchy](https://omarchy.org). It shows the pending changes of a git repository
and a side-by-side one-pane diff for the selected file, and plugs into the
Nautilus context menu as **Open in Omagit** (only shown inside git repositories).

## Features (initial version)

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

Handy flags for development: `--select <repo-relative path>` pre-selects a file,
`--screenshot out.png` renders the window and exits (works with `QT_QPA_PLATFORM=offscreen`),
and `OMAGIT_THEME_DIR=/usr/share/omarchy/themes/tokyo-night` previews another theme.

## Layout

| File | Purpose |
|------|---------|
| `src/OmarchyTheme.*` | Parses `colors.toml`, builds the palette/stylesheet, watches for theme switches |
| `src/GitRepo.*` | Thin wrapper over the `git` CLI: status, diff, commit |
| `src/DiffModel.*` | Unified-diff parser and inline (token LCS) diff |
| `src/DiffView.*` | Custom-painted side-by-side viewer |
| `src/ChangesModel.*` | Table model for the changes list |
| `src/MainWindow.*` | The commit dialog |
| `nautilus/omagit.py` | Nautilus "Open in Omagit" menu provider |
