# Omagit

A small classic *commit dialog* and *history viewer* for Linux, written in
C++/Qt 6 and themed by [Omarchy](https://omarchy.org). It shows the pending changes of
a git repository with a side-by-side diff for the selected file, the commit
history with a branch graph, and plugs into the Nautilus context menu as
**Open in Omagit** (only shown inside git repositories).

## Features

- Changes list with checkboxes, path / extension / size / status / lines added / lines removed,
  coloured by status like a classic git GUI (modified, added, deleted, renamed, conflicted, unversioned).
  Sorted by status by default: modified files first, unversioned last, and within each group by
  folder and then file name (case-insensitive); click a column header to sort by something else.
- Diff view modelled on a classic diff tool: **two-pane side-by-side view by default** (HEAD on
  the left, working tree on the right, aligned row by row with grey filler where one side has
  nothing) and a one-pane unified view (toggle with the *Two-pane* button or Ctrl+T; the choice
  is remembered). Both show the whole file as context in a classic diff tool's own line colours
  (orange removed lines, grey filler; darker variants on dark themes) with green added lines,
  inline (word-level) change highlighting, a margin with +/− icons and line numbers,
  next/previous change navigation (F8 / Shift+F8), text selection
  and copy, optional whitespace markers (Ctrl+W), syntax colouring of the code by file type
  (Ctrl+L, on by default; the *Syntax* button and the context menu toggle it too, and the
  choice is remembered), and Ctrl+wheel or Ctrl+= / Ctrl+- zoom (Ctrl+0 resets).
  Drag the center divider to resize the diff panes;
  double-click it to restore an equal split.
- Commit the checked files with a message (Ctrl+Enter). In the changes list, Space checks or
  unchecks a file and Ctrl+Shift+Space all of them; Ctrl+Shift+A ticks *Amend last commit*, Ctrl+E opens
  the selected file in its own program, Ctrl+D discards its changes (after asking).
- **Commit message from a coding agent**: the sparkle in the top right corner of the message
  box (Ctrl+G) hands the checked changes (all of them when none is checked) to a coding
  agent CLI and puts its answer in the box, the way Cursor's generate button does; Ctrl+Z
  brings your own text back, and a click on the spinner stops the run. The message is one
  imperative subject line naming the essence of the change and, when the change delivers
  more than one thing of value, a bullet per thing — as many as there are, none for details
  or files. The cog at the right of the MESSAGE label picks the **agent** — Claude Code or
  Codex, whichever is installed (Omarchy's default agent to begin with) — the **model** and
  the **reasoning** level. Models and levels are what the CLI itself names: `claude --help`'s
  model aliases and `--effort` levels, `codex debug models`' catalog with each model's own
  levels; *Other…* takes any model by name. The agent runs without tools or a saved session
  (`claude -p --tools ""`, `codex exec --ephemeral --sandbox read-only`); the choice lives
  under `[agent]` in `~/.config/omagit/omagit.conf`.
- **Branch dropdown**: the branch name above the message is a button; clicking it lists the
  local and the remote branches (the current one ticked) and picking one checks it out. A
  search field at the top has the keyboard right away: typing narrows the list, Up/Down move
  the highlight, Return picks the highlighted (else the first) match, Escape closes. A
  remote branch gets a local branch of the same name tracking it (or switches to that local
  branch if it already exists); local changes are carried over, and if git would lose them
  it refuses and says why.
- **Repository dropdown** in the footer: the repository's name (or Ctrl+R) lists the repositories opened
  lately (the last 15, checked = the current one) and *Open…* (Ctrl+O) picks a folder anywhere
  inside another repository. Everything — changes, history, branch, Pull/Push counts, the
  working-tree watch — follows the switch. Started outside a repository without a path,
  Omagit reopens the last one.
- **Pull, Push and Fetch** in the toolbar above the left section (Ctrl+P / Ctrl+Shift+P / Ctrl+F), in
  both modes and on the Mini rail. The Pull button carries a badge with the number of commits waiting on the
  upstream, the Push button the number not pushed yet; a walking-dots badge shows while
  the count is being refreshed and a new number pops in when it changes. To keep the Pull
  count current Omagit fetches by itself: shortly after start, every 3 minutes while the
  window is open, and when the window comes back to the front after a while — a cheap
  `git fetch --all` whose failures back off up to 15 minutes, so an offline machine is
  left alone (`remote/autoFetchSeconds` in `~/.config/omagit/omagit.conf`, 0 turns it off,
  as does `--no-fetch`). Fetches and pushes made in a terminal are picked up through a
  watch on the git directory. Push on a branch without an upstream publishes it
  (`git push -u`); hovering the branch name says which upstream the counts refer to.
- **Signing in**: an https remote whose credentials git does not have, or an ssh key with a
  passphrase and no agent, used to end in git's own `could not read Username`. Omagit is its
  own askpass helper now: git and ssh put their prompt to it, and it puts it to you in a
  themed dialog — username and password (or token) in one go for an https host, since git
  asks for the two separately and one sign-in is one dialog, however often git asks (a fetch
  over several remotes that share a URL — same scheme, host and user — asks once, and one
  that names another user asks for that user); the passphrase alone for a key. Closing the
  dialog ends the asking for the whole operation, remotes still to come included.
  Whether it is remembered is git's business and the dialog says which: a configured
  credential helper (libsecret and friends) keeps it, without one it is used once and
  forgotten — nothing is written by Omagit. Only what you start asks: the automatic fetches
  stay silent and back off as before.
- **Merge** (the toolbar button after Fetch, Ctrl+Shift+M, also on the Mini rail): a merge view
  with the branch to merge on the left and the branch it goes into on the right — the current
  branch to begin with, the main line (or the branch committed to most recently) on the other
  side — each a searchable dropdown, and a swap button between them to merge the other way
  round. As soon as both are set the view says what `git merge` would do, worked out on the
  trees alone (`git merge-tree`, nothing touches the working tree): a *fast-forward*, a clean
  *merge commit* (with the commit and file counts), or *conflicts*, naming the files git could
  not merge on its own. Local changes that git would refuse to overwrite are pointed out and
  block the Merge button. Merging into a branch that is not checked out switches to it first.
  *Always create a merge commit* is `--no-ff`. A merge that ends in conflicts hands over to the
  Changes list: the conflicted files are red, git's own message is in the box, the button reads
  *Commit merge* (a partial commit being impossible mid-merge, it commits what is staged),
  the branch label says *merging …* and the Merge button carries a red mark; opening the
  view again shows the files still unresolved and offers *Abort merge*.
- **Amend last commit**: tick the box (or start with `--amend`) and the message box gets the
  last commit's message while the changes list is compared against the commit before it, so
  the files of the last commit appear checked next to your new changes. *Amend* then rewrites
  HEAD with exactly the checked files (same parents and author); files of the old commit you
  untick go back to the working tree as staged changes. You are warned when the commit is
  already on a remote branch.
- **History** (Ctrl+2, or `--history`): the commits of the current branch (or *All branches*)
  with a lane graph, branch/remote/tag chips, author, date and SHA; filter by message, author
  or SHA (Ctrl+S); commits load 500 at a time as you scroll. Selecting a commit shows its
  details and the files it touched; selecting a file shows the diff against the parent in
  the same diff pane. Right-click a commit to copy its SHA or message.
- The toolbar adapts to the width of the left section: labels give way to icons, and icons
  to a *more* menu (Fetch folds first, Pull last), so the section can be dragged as narrow
  as you like; the diff pane can be dragged just as narrow. The toolbar's first button is
  the Docked/Mini toggle; Refresh is the small icon next to the *n / m selected* count (and
  next to *All branches* in History), F5 or Ctrl+Shift+R works everywhere.
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
  re-themes itself live when you run `omarchy theme set …` or `omarchy display text size …`
  (the whole window re-flows to the new base size; a Ctrl+wheel zoom of the diff stays as an
  offset on top of it).
- Refreshes when the working tree or index changes.

## Build & install

Requires `qt6-base` and `git`; the context menu needs `nautilus-python`.

```bash
./install.sh
```

This builds with `qmake6`, installs `~/.local/bin/omagit`, a desktop entry, an icon and
`~/.local/share/nautilus-python/extensions/omagit.py`, then restarts Nautilus so the menu
appears. `./uninstall.sh` removes everything again.

To build without installing, `qmake6 omagit.pro && make` — optimised, like the installer;
`qmake6 CONFIG+=debug omagit.pro && make` builds with debug symbols instead. The source
lists live in `omagit.pri`, which the app and the test projects share, so a new file is
registered in one place.

## Usage

```bash
omagit [path]            # any directory or file inside a repository (default: cwd)
```

`--history` opens the history view, `--amend` starts with *Amend last commit* ticked,
`--mini` starts in the Mini layout, `--full` with the diff pane hidden, and `--no-fetch` leaves the network alone.

The footer's info button or **Ctrl+K** opens the keybindings panel, styled after Omarchy's
own Super+K menu: type to filter, ↑/↓ move the cursor, **Enter** runs the highlighted
binding, **Esc** closes. The keys follow lazygit's letters with Ctrl in front (Ctrl+Shift for
its capitals): Ctrl+F fetch, Ctrl+P pull, Ctrl+Shift+P push, Ctrl+Shift+M merge, Ctrl+Shift+Space
check all, Ctrl+Shift+A amend, Ctrl+E open, Ctrl+D discard, Ctrl+R recent repositories, Ctrl+S
filter the history, Ctrl+W whitespace, Ctrl+L syntax colours, Ctrl+Shift+R (or F5) refresh,
Ctrl+Q quit; Ctrl+1, Ctrl+2 and Ctrl+3 are the commit view, the history and the branch list.

Handy flags for development: `--select <repo-relative path>` pre-selects a file,
`--screenshot out.png` renders the window and exits (works with `QT_QPA_PLATFORM=offscreen`;
`--screenshot-after <ms>` sets the delay, e.g. to catch a running fetch; `--screenshot-menu
branch|repo|agent|keybindings|merge|login` opens that panel first and includes it in the picture, `--screenshot-keys
m,a,Down,Return` then types into it — or, without a menu, sends the keys to the window, so
`--screenshot-keys Ctrl+G --screenshot-after 45000` shows a generated message),
and `OMAGIT_THEME_DIR=/usr/share/omarchy/themes/tokyo-night` previews another theme.

## Tests

Three self-contained suites, all built and run by one script:

```bash
tests/run.sh            # every suite
tests/run.sh ui         # only the named ones: gitrepo, mergedialog, ui
```

Every suite gets a throw-away `XDG_CONFIG_HOME`, so a run never touches the real
`omagit.conf`, and the widget suites render offscreen with the Fusion style.

- `gitrepo` (`tests/gitrepo_test.cpp`) exercises the git wrapper against throw-away
  repositories it builds itself: status, amend, history, fetch/pull/push, branches and
  checkout, merging, root switching, and the sign-in server — the prompts git and ssh send,
  the helper's end of the socket, and the password a username prompt leaves for the
  password prompt that follows. Core only — it is built with `QT -= gui`, which is
  what keeps `GitRepo` and its neighbours free of QtGui.
- `mergedialog` (`tests/mergedialog_test.cpp`) drives the merge view: swapping the
  branches, the verdicts and the layout that must not jump while one is checked.
- `ui` (`tests/ui_test.cpp`) covers the logic behind the widgets: the history graph's
  lane layout, the changes list's check marks, the toolbar's overflow, the keybindings
  filter, `colors.toml` parsing with its fallbacks, the wording of the merge verdict, and
  the sign-in dialog — including one whole round trip through git itself, where
  `git credential fill` asks the built binary, which asks the dialog (no network involved;
  the test is skipped when the binary is not built).

## Layout

| File | Purpose |
|------|---------|
| `src/OmarchyTheme.*` | Parses `colors.toml`, builds the palette/stylesheet, watches for theme switches |
| `src/GitRepo.*` | Thin wrapper over the `git` CLI: status, diff, commit, amend, log, refs, branches and checkout, merge preview / merge / abort, upstream state, async runs, switchable root |
| `src/RemoteSync.*` | Fetch / pull / push, the ahead/behind counts, automatic fetching with backoff, git-dir watch |
| `src/AskPass.*` | Omagit as its own askpass helper: the socket git's and ssh's prompts arrive on, what they mean, one sign-in per operation, and the client side of `--askpass` |
| `src/BadgeButton.*` | Tool button with a count badge, busy dots and a pop-in animation |
| `src/Toolbar.*` | Width-adaptive button row: labels → icons → "more" menu |
| `src/TickMenu.*` | Menu whose checked entries carry an accent tick at the right edge instead of a checkbox (branch, repository, "more" and diff menus) |
| `src/BranchMenu.*` | The searchable branch dropdown (footer branch button, both sides of the merge view) |
| `src/MergeDialog.*` | The merge view: source/destination pickers with swap, the merge-tree verdict, merge and abort |
| `src/LoginDialog.*` | The sign-in: username and password for an https host, an ssh key's passphrase, and whether git will remember it |
| `src/DiffModel.*` | Unified-diff parser and inline (token LCS) diff |
| `src/DiffView.*` | Custom-painted side-by-side viewer |
| `src/SyntaxHighlighter.*` | Hand-rolled per-language tokeniser for the diff's syntax colours |
| `src/ChangesModel.*` | Table model for the changes list (also the files of a commit) |
| `src/HistoryModel.*` | Commit list model with incremental lane-graph layout |
| `src/HistoryView.*` | History view: filter, commit table with graph and ref chips, details, files |
| `src/MainWindow.*` | Window shell: modes and sync buttons, Docked/Mini layouts, the pages and the diff pane, branches, merging, repositories and the working-tree watch |
| `src/CommitPage.*` | The commit dialog page: message box with the coding-agent flow, changes list with its context menu, options and the Commit button |
| `src/DiffPane.*` | The right pane: Prev/Next and the two-pane / whitespace / syntax toggles above the diff view, with their remembered settings |
| `src/Footer.*` | The footer bar: layout toggle, repository and branch dropdowns, the path or the latest message, keybindings button |
| `src/UiHelpers.*` | The shell's Nerd Font glyphs and the small widget factories the sections share (section and dim labels, tool / small / dropdown buttons, hairline, menu headers) |
| `src/DesktopExec.*` | A file's default application, read from its desktop entry, and launching it detached |
| `src/MiniRail.*` | The Mini layout's rail: file miniatures with instant path tooltips |
| `src/PaneLayout.h` | The Docked/Mini enum with its glyphs, names and settings keys |
| `tests/gitrepo_test.cpp` | Checks for status, amend, history, fetch/pull/push, branches/checkout, merging and root switching against scratch repositories |
| `nautilus/omagit.py` | Nautilus "Open in Omagit" menu provider |
