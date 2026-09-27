# Omagit

**An Omarchy-native git GUI, built for tiling window managers.**

On a tiling desktop a window rarely keeps its size. Open a browser beside Omagit and it
gets half the screen; open a terminal as well and it is down to a quarter. Omagit is made
for that: as its tile shrinks it rearranges itself instead of clipping. Labels turn into
icons, less-used buttons move into a menu, and below 700 px the window shows one thing at
a time — Changes, Diff or History — behind tabs, with the diff in a single column.
Committing, reviewing a diff, browsing the history and pulling or pushing keep
working down to a 340 px tile, and when the window widens again the full layout comes back
as you left it, with the same selection and the same place in the diff.

It looks and behaves like part of [Omarchy](https://omarchy.org): colours, fonts, text
size and icons come from the active Omarchy theme and follow `omarchy theme set` and
`omarchy display text size` live, the controls follow the Omarchy shell's style, and the
keyboard shortcuts use lazygit's letters with Ctrl in front.

![The commit view in a full-width window: the changes as a tree, the message box under it, and a side-by-side diff](docs/screenshots/commit.png)

![The same window as a half tile and as two 470 px tiles: the Changes tab and the Diff tab](docs/screenshots/tiles.png)

![The history view with the branch graph, commit details and the diff of a file](docs/screenshots/history.png)

- **Commit** — pending changes as a tree, a compact list or a table, a
  side-by-side diff (side by side or unified, word-level highlights, syntax
  colours), amend and discard, and an optional commit message written by Claude Code or
  Codex.
- **History** — the whole history with a branch graph, ref labels and search, and each
  commit's details, files and diffs.
- **Branches** — switch, create, and merge with a preview that says beforehand whether
  the merge will conflict.
- **Sync** — fetch, pull and push with ahead/behind counts and automatic fetching, and
  sign-in prompts for HTTPS and SSH remotes that can be remembered in the keyring.
- **Repositories** — recent repositories, any folder, cloning from a URL or from your
  GitHub repositories, and an optional **Open in Omagit** entry in Nautilus.

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
  The row above the diff folds with the pane's width instead of cutting its labels short:
  in a pane narrower than 900 px (scaled with the text size) the view options become glyphs,
  and under 560 px Prev and Next do too, the counter reads `1/3` and the three options move
  into a `…` menu.
- Commit the checked files with a message (Ctrl+Enter; in the Mini layout it opens the commit
  popover first, and commits from it once it is open). In the changes list, Space checks or
  unchecks a file and Ctrl+Shift+Space all of them; Ctrl+Shift+A ticks *Amend last commit*, Ctrl+E opens
  the selected file in its own program, Ctrl+D discards its changes (after asking).
- **Commit message from a coding agent**: the sparkle in the top right corner of the message
  box (Ctrl+G) hands the checked changes (all of them when none is checked) to a coding
  agent CLI and puts its answer in the box, the way Cursor's generate button does; Ctrl+Z
  brings your own text back, and a click on the spinner stops the run. The message is one
  imperative subject line naming the essence of the change and, when the change delivers
  more than one thing of value, a bullet per thing — as many as there are, none for details
  or files. The cog at the right of the MESSAGE label opens the **agent settings**, a card
  hanging under it (beside the Mini commit card when opened from its cog): AGENT picks Claude Code or Codex, whichever is installed (Omarchy's
  default agent to begin with, and named as such), MODEL lists the models with the id the
  CLI takes and a tick on the chosen one, REASONING is a track of the levels (click a stop,
  or Left/Right). Models and levels are what the CLI itself names: `claude --help`'s model
  aliases and `--effort` levels, `codex debug models`' catalog with each model's own levels;
  *Other model…* opens a field that takes any model by name (Return saves it, Escape backs
  out). Every choice is saved the moment it is made; *Generate now* closes the card and asks
  at once, Escape, the cog again or a click elsewhere just close it. With no agent installed
  the card says so and offers `omarchy default agent claude` / `codex`, each with a copy
  button. The agent runs without tools or a saved session
  (`claude -p --tools ""`, `codex exec --ephemeral --sandbox read-only`); the choice lives
  under `[agent]` in `~/.config/omagit/omagit.conf`.
- **Branch dropdown**: the branch name in the top bar is a button; clicking it lists the
  local and the remote branches (the current one ticked) and picking one checks it out. A
  search field at the top has the keyboard right away: typing narrows the list, Up/Down move
  the highlight, Return picks the highlighted (else the first) match, Escape closes. A
  remote branch gets a local branch of the same name tracking it (or switches to that local
  branch if it already exists); local changes are carried over, and if git would lose them
  it refuses and says why.
- **Repository dropdown** in the top bar: the repository's name (or Ctrl+R) lists the repositories opened
  lately (the last 15, checked = the current one) and *Open…* (Ctrl+O) picks a folder anywhere
  inside another repository. Everything — changes, history, branch, Pull/Push counts, the
  working-tree watch — follows the switch. Started outside a repository without a path,
  Omagit reopens the last one. With no repository to reopen, it offers cloning or opening an existing repository.
- **Clone repository**: *Clone…* in the repository dropdown (Ctrl+Shift+O) accepts HTTPS,
  HTTP and SSH URLs, including `git@host:owner/repo.git`. Choose a destination folder; the
  repository is created inside it and opened when cloning finishes. The folder defaults
  to the current directory, or the open repository's parent. Existing destinations are
  left alone. The prefilled repository folder name can be edited inline in the destination preview.
  Progress and credential prompts stay in the app; *Stop* cancels the transfer
  and leaves any partial download in place. The clone counts as the new repository's first
  fetch, so Omagit does not fetch it again the moment it opens.
  The **GitHub** tab uses [GitHub CLI](https://cli.github.com/) (`gh`, optional), offers
  browser sign-in, and lists the current account's personal, organization and shared
  repositories with a filter. GitHub credentials are managed by `gh`; clones from this tab
  save the GitHub credential helper in the new repository so later fetches, pulls and
  pushes use the same login.
- **Pull, Push and Fetch** in the top bar (Ctrl+P / Ctrl+Shift+P / Ctrl+F), in
  both modes and in every layout. The Pull button carries a badge with the number of commits waiting on the
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
  credential helper (libsecret and friends) keeps it. Without one the dialog offers
  **Remember this sign-in**, ticked: once the fetch, pull, push or clone worked, Omagit hands
  the login to git's libsecret helper (`git credential approve`), which keeps it in your
  keyring, and then adds `credential.<scheme>://<host>.helper = libsecret` for that one server
  to your global git configuration, so git asks the keyring first and the server stops asking
  — Omagit itself writes no password anywhere. (A configuration that turns credential helpers
  off for the remote gets no such offer.) (Every Omarchy machine has the helper and the keyring; Omarchy's keyring has no
  password of its own, so a personal access token is the better thing to keep there, as the
  dialog says.) Untick it and the login is used once and forgotten. The note names the helper by its program
  (`store`, `libsecret`, `gh`) and shows its whole command on hover; a clone's sign-in reads
  git's global configuration, since a clone uses no repository's. For a plain-HTTP remote the
  dialog warns that the password is sent unencrypted (this machine's loopback excepted). Only
  what you start asks: an automatic fetch runs with no askpass at all (not even one the
  session set up, nor the system's ssh-askpass), and one that runs into a sign-in — a password,
  an ssh key to unlock, a host key to confirm — says "Sign-in needed for *host* — Fetch (Ctrl+F)
  to sign in" in the footer instead of an error, leaves the Fetch button unmarked and backs
  off as before. A host whose key changed stays a loud error. ssh's question about a host it
  has never seen becomes **Trust *host*?** with the key's fingerprint and a *Trust and connect*
  button (the "yes" ssh wants); a failed fetch, pull or push shows ssh's own reason
  ("Permission denied (publickey)") with git's full output under it.
- **SSH keys**: when the server turns down the key ssh offered, the error offers **Choose SSH
  key…** — a list of the key pairs in `~/.ssh` (name, type, comment; path and fingerprint on
  hover), ssh's own choice, or any other key file. The choice becomes the repository's
  `core.sshCommand` (`ssh -i <key> -o IdentitiesOnly=yes`): for that repository ssh offers the
  key first, and after it only keys your ssh config names (`IdentityFile`, on disk or in the
  agent) — not the rest of the agent's keys — and the operation runs again. The Clone dialog has the same choice as an **SSH key**
  field under an ssh URL whenever ssh would not find the key by itself (more than one pair, or
  one under a name of its own); `git clone -c core.sshCommand=…` saves it in the new repository.
  An exported `GIT_SSH_COMMAND` outranks the setting, and both windows say so.
- **Key passphrases**: with an ssh-agent reachable (`SSH_AUTH_SOCK` set and answering, as a desktop
  session normally provides), the passphrase dialog offers **Keep unlocked until logout**, ticked:
  once the operation worked, Omagit runs `ssh-add <key>` and answers its prompt from memory through
  its own askpass, so the agent keeps the key and ssh stops asking. Without an agent (a bare
  Hyprland session starts none) there is no box, and the note says plainly that the passphrase is
  asked for every time.
- **Merge** (the top bar's button after Fetch, Ctrl+Shift+M): a merge view
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
- **New branch** (Ctrl+N, the branch list's last row, or *New branch from here…* on a commit in
  the history): a card under the branch chip with the name and where the branch starts — the
  current branch, another branch, a remote branch, a tag or the commit picked in the history —
  and the commit that is. A space in the name becomes a dash; a name git refuses or a branch
  that exists already is said in red (with *Switch to it* for the latter). *Switch to it* checks
  the new branch out with your changes along (`git switch -c`), off it only makes the branch
  (`git branch`); where changed files differ at the start the card names them and only making
  the branch is possible. A remote branch fills an empty name with its own and is tracked.
  Typing a name no branch has into the branch list leaves *New branch “…”* as its only row, so
  Enter takes the name to the card.
- **Amend last commit**: tick the box (or start with `--amend`) and the message box gets the
  last commit's message while the changes list is compared against the commit before it, so
  the files of the last commit appear checked next to your new changes. *Amend* then rewrites
  HEAD with exactly the checked files (same parents and author); files of the old commit you
  untick go back to the working tree as staged changes. You are warned when the commit is
  already on a remote branch.
- **History** (Ctrl+2, or `--history`): the commits of the current branch (or *All branches*)
  with a lane graph, branch/remote/tag chips, author, date and SHA; the filter (Ctrl+S)
  searches the whole history by message, author, e-mail or SHA, the matches showing as git
  finds them. Commits load 500 at a time as you scroll, and so do the filter's matches;
  every page walks the history as it stood at the last refresh, so a commit or a fetch in
  between neither repeats nor skips a commit. Loading a second page of either writes git's
  commit-graph file if the repository has none (as `git gc` does), which keeps the later
  pages fast. Selecting a commit shows its details
  and the files it touched; selecting a file shows the diff against the parent in the same
  diff pane. Right-click a commit to copy its SHA or message.
- **Top bar**: one row above the whole window, in every layout — the repository and the
  branch chip on the left, the **Changes *n* | History** tabs centred in the window (the
  count is what the changes list shows, whichever tab is on), and Pull, Push, Fetch, Merge
  with the two layout toggles on the right. A narrower window folds it in order: the sync
  labels give way to icons, then Fetch and Merge move into a *more* menu (which wears an
  accent dot while what it holds carries a count), then the tab labels give way to their
  glyphs, then the last two sync buttons follow into the menu, and only when nothing else is
  left do the repository and branch names elide, sharing the room evenly (a short name stays
  whole while the longer one gives way). The repository chip always wears its name, the
  tabs keep 16 px clear of both groups, and the toggles are never hidden. Refresh is the small icon at the right of the *CHANGES* row (and next to
  *All branches* in History), F5 or Ctrl+Shift+R works everywhere.
- The *CHANGES* title carries the count (*CHANGES · 5/7*: checked / listed) and the button says
  what it will take (*Commit 5 files*). The box in the table's header checks or unchecks every
  listed file, the eye beside Refresh shows or hides the unversioned files — hidden files are
  never checked, so they are never committed — and *Amend last commit* sits at the left of the
  bottom bar, next to the button it renames.
- **Three ways to list the pending files**, the three small buttons at the right of the *CHANGES*
  row, Tree first (the choice is remembered; until one is made, it is the tree at every
  width): *Tree* puts the files under their directories — directories first and
  alphabetically, 14 px per level, a chevron and a folder icon, and `3 files` after a folded
  one; *Compact* is the table down to the checkbox, the file name and a narrow status pill,
  with the file's folder after it in dim, smaller type; *Table* is the full list with its
  columns, sorting and right-click actions. In the tree, a directory's checkbox stands for every file
  under it, folded away or not (partial when only some are checked), Space checks the row the
  keyboard is on, ←/→ close and open a branch, and a click on the chevron opens it without
  touching a check mark. Whichever list is on show, it is the same files with the same check
  marks and the same current file: switching changes nothing but the presentation. Which
  directories are folded is kept for the session — sorting, filtering and refreshes leave it
  alone — but not across runs.
- **Docked / Mini** layouts (the first of the top bar's two toggles or Ctrl+B; the choice is
  remembered). *Docked* keeps the left section (commit dialog or history) next to the diff
  pane. *Mini* shrinks it to a narrow rail of file miniatures (extension tile, status letter,
  dimmed when not part of the commit) so the diff gets the whole window; hovering a miniature
  pops up its name and folder right away, Space or Ctrl+click toggles it for the commit.
  At the bottom of the rail, in the commit view, an accent **commit tile** carries the number
  of checked files in its badge; clicking it (or Ctrl+Enter) opens the **commit popover**
  beside it — MESSAGE with the agent cog and the sparkle, how many of the listed files are
  selected, *Amend last commit* and a primary Commit. It is the commit page's own message
  (the same text and undo history, the agent streaming into it) and its own Amend, agent
  settings and commit, so nothing typed in one place is missing in the other. The card grows
  upwards with its text, up to a third of the window. Escape, a second click on the tile, a
  click elsewhere in the window, a successful commit, the history, the Docked layout and
  another repository close it; the rail stays usable while it is open — selecting,
  Ctrl+click, Space and Refresh leave it where it is, and so does F5.
  The second toggle, in the window's top right corner (Ctrl+Shift+B), hides the diff pane so
  the left section fills the window; double-clicking a file brings it back for that file
  (with the pane showing, a double-click opens the file in its own program instead).
  Right-clicking a file offers *Open with …*, naming the program the desktop would use
  for it (from `xdg-mime`).
- **Stacked** below 700 px (scaled with the text size, like every design pixel): the body
  shows one thing at a time, picked by three tabs — **Changes *n* | Diff | History**. Changes
  and History fill the body with their page; Diff is the Mini rail beside the diff pane, with
  the files of the mode of the moment (the commit tile and its popover included). The window
  opens on the tab its layout stands for (Mini is Diff), Ctrl+B and Ctrl+Shift+B switch
  between the page and Diff, and a double-click on a file shows its diff. It is presentation
  only: the Docked/Mini choice, the hidden diff pane and the left section's width are left
  as they were and come back as the window widens — the selection, the scroll offsets and
  the place in the diff too. The top bar folds on its own three levels there (tab labels,
  then glyphs, then two rows where the names elide): the repository keeps its name, Pull
  and Push become one borderless **sync dropdown** carrying both counts (`↓2 ↑1`, the
  walking dots while one runs, Merge's mark in the corner) whose menu lists Pull, Push,
  Fetch and Merge…, and the layout toggles hide. **More** is there at every stacked width, and on any row it also
  carries Refresh, Open repository…, Clone… and Keybindings after whatever sync buttons are
  folded into it. The commit page's action bar folds too: an **options** `…` at the left
  (Amend last commit, opening upwards) and Commit across the rest of the row. At the
  narrowest widths (a 340 px tile), where even the tab glyphs would crowd the two names,
  the tabs take a row of their own under the controls, with More at its end (its menu opens under it), and the names on the first row give way only
  where the sync dropdown at its right would come within 4 px of the branch; the page's
  CHANGES and MESSAGE header rows fold away too, and their controls lead the More menu on
  the Changes tab: a **Files view** submenu (Tree, Compact list, Table), Show
  unversioned files, and Agent settings…, which hangs the agent card from More. A shallow
  window (under 560 px tall at the design's text size) folds them the same way at any width,
  and the top bar keeps More for them even where no sync button is folded into it.
  The diff is unified while stacked, at the line it was on; Ctrl+T still switches there,
  without saving, and the remembered view comes back as the window widens.
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

## Install

Omagit needs `qt6-base`, `git` and a Nerd Font for its icons (every Omarchy install has
one). Optional: `openssh` for SSH remotes, `libsecret` to remember sign-ins,
`github-cli` to clone from your GitHub list, `nautilus-python` for the Nautilus entry, and
Claude Code or Codex for generated commit messages.

From the AUR (`omagit` is the latest release, `omagit-git` the latest commit):

```bash
omarchy pkg aur add omagit
```

From source, for the current user:

```bash
./install.sh     # builds with qmake6, installs ~/.local/bin/omagit, a desktop entry and an icon
./uninstall.sh   # removes them again
```

A copy in `~/.local/bin` comes before a packaged `/usr/bin/omagit` on the PATH, so run
`./uninstall.sh` before switching to the package.

Nautilus's **Open in Omagit** entry is not part of the install: tick *Show "Open in Omagit"
in Nautilus* in Settings (the footer's cog, More → Settings…, or **Ctrl+,**), which writes
`~/.local/share/nautilus-python/extensions/omagit.py` and offers to restart a running
Nautilus so it loads the extension; unticking removes the file again. `./uninstall.sh`
removes everything, the extension included.

To build without installing, `qmake6 omagit.pro && make` — optimised, like the installer;
`qmake6 CONFIG+=debug omagit.pro && make` builds with debug symbols instead. The source
lists live in `omagit.pri`, which the app and the test projects share, so a new file is
registered in one place. `make install` installs under `PREFIX` (`qmake6 PREFIX=/usr`,
default `/usr/local`) and into `INSTALL_ROOT` when set. Packages build with
`CONFIG+=no_screenshot_keys`, which leaves out the test-only `--screenshot-keys` and with
it Qt's private API, so a Qt update does not break the binary; the recipes for the AUR
and the Omarchy package repository, and the release steps, are in
[`packaging/`](packaging/README.md).

## Usage

```bash
omagit [path]            # any directory or file inside a repository (default: cwd)
```

`--history` opens the history view, `--amend` starts with *Amend last commit* ticked,
`--mini` starts in the Mini layout (in a window under the stacking width, the Diff tab), `--full` with the diff pane hidden, and `--no-fetch` leaves the network alone.
`--mini --screenshot-menu commit` pictures the Mini layout with its commit popover open;
`--screenshot-menu agent` the agent settings under the page's cog (under More where the
header rows are folded away), and with `--mini` under the commit popover's cog (the popover
opens too).

The footer's keyboard button or **Ctrl+K** opens the keybindings panel, styled after Omarchy's
own Super+K menu: type to filter, ↑/↓ move the cursor, **Enter** runs the highlighted
binding, **Esc** closes. The keys follow lazygit's letters with Ctrl in front (Ctrl+Shift for
its capitals): Ctrl+F fetch, Ctrl+P pull, Ctrl+Shift+P push, Ctrl+Shift+M merge, Ctrl+Shift+Space
check all, Ctrl+Shift+A amend, Ctrl+E open, Ctrl+D discard, Ctrl+R recent repositories, Ctrl+S
filter the history, Ctrl+W whitespace, Ctrl+L syntax colours, Ctrl+Shift+R (or F5) refresh,
Ctrl+Q quit, Ctrl+, settings; Ctrl+1, Ctrl+2 and Ctrl+3 are the commit view, the history and the branch list;
Ctrl+N a new branch (lazygit's n) — from the current branch, from the selected commit in the
history, and named after the search in the branch list; in its card Enter creates the branch
and Esc closes it.
Ctrl+Enter commits in the commit view; the panel lists it for the *Commit view, Mini rail*,
because in the Mini layout the same keys open the commit popover, and commit from it once
it is open (the keypad's Enter works too). Plain Enter in a message box is a new line.

Handy flags for development: `--select <repo-relative path>` pre-selects a file,
`--screenshot out.png` renders the window and exits (works with `QT_QPA_PLATFORM=offscreen`;
`--screenshot-after <ms>` sets the delay, e.g. to catch a running fetch; `--screenshot-size
945x612` gives the window a fixed size first, so a picture does not depend on the desktop it
was taken on — two positive numbers, only together with `--screenshot`, and anything else is
a usage error; `--screenshot-menu
branch|repo|agent|keybindings|merge|login|commit|newbranch|sync|more|options|diff` opens that panel first and includes it in the picture
(`commit` is the Mini layout's commit popover, so it goes with `--mini`; elsewhere it opens nothing;
`agent` opens nothing in the history; `sync` and `options` are the stacked layout's sync dropdown
and action-bar menus and open nothing on a wider window, `more` only where the More button is
shown, `diff` the diff pane's `…` view options, only where the pane is narrow enough to show
that button, `newbranch` the New branch card from the current branch), `--screenshot-keys
m,a,Down,Return` (development builds only, see Install) then types into it — or, without a menu, sends the keys to the window, so
`--screenshot-keys Ctrl+G --screenshot-after 45000` shows a generated message; `@changesTable`
and `@changesTree` in that list focus the list on show, so the keys reach it),
`--files-view tree|compact|table` lists the pending files that way for one run (exactly those
three lowercase names; anything else is a usage error and exits 2), whatever is remembered and
without remembering it — it works on its own as well as with `--screenshot`, and with
`--history` or `--mini` it only sets up the commit page,
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
  lane layout, the changes list's check marks, the top bar's seven folding levels (with the
  tab clamp, the more menu and the count pill) and the one window it sits in, the keybindings
  filter, `colors.toml` parsing with its fallbacks, the wording of the merge verdict, and
  the sign-in dialog — including one whole round trip through git itself, where
  `git credential fill` asks the built binary, which asks the dialog (no network involved;
  the test is skipped when the binary is not built). The files-view switcher has a group of
  its own: the tree model over the flat proxy (its shape, its ordering and the round trip
  between a file row and the proxy's, checked with `QAbstractItemModelTester`), directory
  checking down to folded branches and out to a rename whose old path spells another row's,
  the keyboard and the chevron/checkbox hit testing, which changes rebuild the tree and which
  only repaint it, the collapsed set surviving sorting, filtering and reloads, one current
  file across all three lists (proved with `QSignalSpy`: no second notification for the same
  file or a mode-only switch), the refresh sequence putting the selection and the scroll back,
  compact's three columns and the table's own widths coming back, the remembered and
  overridden choice, and all of it measured again after a live 12 → 16 → 12 text-size change
  against a page built fresh at that size. The Mini layout's commit tile and popover have
  theirs: the tile's visibility, geometry and badge (every way a check mark changes, and a
  badge that listens to its source of the moment only), the rail's Ctrl+click focus, every
  way the card opens and closes and what leaves it open (rail work, the wheel, Refresh, F5,
  other windows and the modal warning of an empty message), its anchor beside the tile, the
  one shared document with its undo stack and its wrapping at the width of the box on
  screen, the mirrored controls and generate button, commits and amends against scratch
  repositories, Ctrl+Enter in both layouts, the keybindings row, the screenshot flag run
  through the built binary, and the rail and the card measured again after a live
  12 → 16 → 12 change against a window built fresh. The stacked layouts have a group too:
  the top bar's three stacked levels (exact thresholds, the clamp, the branch floor, the size
  hints, the controls it shows and hides, and a canonical ordinary row after unstacking), the
  three tabs and their silent synchronisation, a segment strip hiding and showing a segment,
  the sync dropdown's counts, busy dots and Merge mark and its menu, the fuller More menu,
  the window crossing the stacking width (thresholds, visibility, splitter widths, the
  preference setters' mappings and no settings written), the startup tab for each flag and a
  restored narrow Mini geometry, tabs and keys and double-clicks moving between the
  presentations without reloading, the commit and agent cards on the Diff tab, the
  screenshot slots and their guards, the folded action bar and its options menu, the
  tree as the files view at every width until one is picked, the unified stacked diff (the
  same top line, and the same selected text or none) and the remembered view it leaves
  alone, selection, scroll and diff position kept across every transition and a refresh, a window asked for at a
  stacked width shown at that width, and all of it after a live 12 → 16 → 12 change.

## Layout

| File | Purpose |
|------|---------|
| `src/OmarchyTheme.*` | Parses `colors.toml`, builds the palette/stylesheet, watches for theme switches |
| `src/GitRepo.*` | Thin wrapper over the `git` CLI: status, diff, commit, amend, log, refs, branches and checkout, merge preview / merge / abort, upstream state, async runs, switchable root |
| `src/RemoteSync.*` | Fetch / pull / push, the ahead/behind counts, automatic fetching with backoff, git-dir watch |
| `src/AskPass.*` | Omagit as its own askpass helper: the socket git's and ssh's prompts arrive on, what they mean, one sign-in per operation, and the client side of `--askpass` |
| `src/BadgeButton.*` | Tool button with a count badge, busy dots and a pop-in animation |
| `src/Segmented.*` | The painted segmented control: segments with a glyph, a label and a count pill inside one shared frame, at their own widths or stretched equally (the top bar's tabs, the agent picker) |
| `src/TopBar.*` | The window's top row: repository and branch chips, the painted Changes / Diff / History tab segments, the sync buttons with their "more" menu (and the window's Refresh, Open, Clone and Keybindings), the layout toggles — and the seven folding levels that fit them into the width; stacked, its own three levels with the sync dropdown |
| `src/TickMenu.*` | Menu whose checked entries carry an accent tick at the right edge instead of a checkbox (branch, repository, "more" and diff menus) |
| `src/BranchMenu.*` | The searchable branch dropdown (the top bar's branch chip with its New branch row, both sides of the merge view, the New branch card's From picker with the tags) |
| `src/BranchPicker.*` | The field-like button showing a branch, tag or commit that opens the branch dropdown (the merge view's two sides, the New branch card's From) |
| `src/NewBranchCard.*` | The New branch card: name, where it starts, what becomes of the changed files, Switch to it and Create branch |
| `src/MergeDialog.*` | The merge view: source/destination pickers with swap, the merge-tree verdict, merge and abort |
| `src/SettingsDialog.*` | Settings: for now whether Nautilus offers Open in Omagit, with a restart of a running Nautilus to show the change |
| `src/NautilusMenu.*` | Puts the embedded Nautilus extension in the user's extension folder or takes it away, and whether Nautilus, nautilus-python and a running Nautilus are there |
| `src/LoginDialog.*` | The sign-in: username and password for an https host, an ssh key's passphrase, and whether git will remember it |
| `src/CredentialKeeper.*` | Remembers a sign-in the user asked to keep: names git's libsecret helper for that server and hands the login to `git credential approve` |
| `src/AgentKeeper.*` | Keeps a key unlocked until logout: `ssh-add` with the passphrase fed through an askpass of its own |
| `src/SshKeys.*` | The key pairs in `~/.ssh` (type, comment, fingerprint) and a repository's `core.sshCommand` as the way it picks one |
| `src/SshKeyDialog.*` | The key picker offered after a refused key: writes `core.sshCommand` and runs the operation again |
| `src/MessageDialog.*` | Errors, warnings and questions (a failed pull or push, discarding a change) in the dialog kit instead of a QMessageBox, never wider than the window it opens over |
| `src/DiffModel.*` | Unified-diff parser and inline (token LCS) diff |
| `src/DiffView.*` | Custom-painted side-by-side viewer |
| `src/SyntaxHighlighter.*` | Hand-rolled per-language tokeniser for the diff's syntax colours |
| `src/ChangesModel.*` | Table model for the changes list (also the files of a commit), the shared header with its check-all box and per-presentation labels, and the table setup with its compact form |
| `src/ChangesTreeModel.*` | The directory tree over that flat list: three columns, derived directory check states written back in one batch, and a rebuild on every change of the list's shape |
| `src/HistoryModel.*` | Commit list model with incremental lane-graph layout |
| `src/HistoryView.*` | History view: filter, commit table with graph and ref chips, details, files |
| `src/MainWindow.*` | Window shell: the top bar above the body, modes and sync operations, Docked/Mini layouts and the stacked presentation below the stacking width (its tabs, keys and transitions), the pages and the diff pane, branches, merging, repositories and the working-tree watch |
| `src/CommitPage.*` | The commit dialog page: message box with the coding-agent flow, the changes list in its three presentations (table, compact and tree, with their delegates and the switcher), its context menu, options and the Commit button — folded into the stacked action bar's options menu below the stacking width |
| `src/DiffPane.*` | The right pane: Prev/Next and the two-pane / whitespace / syntax toggles above the diff view, with their remembered settings |
| `src/Footer.*` | The footer bar: the repository path or the latest message, and the settings and keybindings buttons |
| `src/UiHelpers.*` | The shell's Nerd Font glyphs and the small widget factories the sections share (section and dim labels, tool / small / dropdown buttons, hairline, menu headers, a drop-down menu kept inside its window) |
| `src/DesktopExec.*` | A file's default application, read from its desktop entry, and launching it detached |
| `src/MiniRail.*` | The Mini layout's rail: file miniatures with instant path tooltips, the commit's hash in history mode, Refresh and the commit tile |
| `src/CommitPopover.*` | The Mini layout's commit popover: an overlay beside the rail's commit tile sharing the commit page's message document, mirroring its Amend, generate and Commit controls, anchored to the tile and closed by Escape, outside presses and leaving the Mini commit view |
| `src/AgentPopover.*` | The agent settings card under a cog (the page's, or the commit popover's): agent picker, model rows, the other-model field, the reasoning level track and Generate now, or the install commands when no agent is installed; every choice saved through the commit page at once |
| `src/PaneLayout.h` | The Docked/Mini enum with its glyphs, names and settings keys |
| `tests/gitrepo_test.cpp` | Checks for status, amend, history, fetch/pull/push, branches/checkout, merging and root switching against scratch repositories |
| `nautilus/omagit.py` | Nautilus "Open in Omagit" menu provider, built into the app and installed from Settings |

## License

MIT — see [LICENSE](LICENSE).
