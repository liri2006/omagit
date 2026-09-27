# Add Omagit

<!-- Draft description for the pull request to omacom/omarchy-pkgs.
     Fill in the TODOs before opening it. -->

Adds `omagit`, [Omagit](https://github.com/liri2006/omagit) 0.2.0: an Omarchy-native git
GUI built for tiling window managers.

A tiled window seldom keeps its size: a browser opened beside it halves it, a terminal
quarters it. As its tile shrinks, Omagit rearranges itself instead of clipping. Labels turn
into icons, less-used buttons move into a menu, the side-by-side diff becomes one column,
and below 700 px it shows one thing at a time (Changes, Diff or History) behind tabs.
Committing, reviewing diffs, browsing the history and syncing keep working down to a
340 px tile, and widening the window brings the full layout back with the same selection
and place in the diff. It reads the active Omarchy theme (`colors.toml`, the shell font and
text size, `icons.theme`) and follows `omarchy theme set` and `omarchy display text size`
live. The keys follow lazygit's letters with Ctrl.

It covers the commit dialog (tree, compact or table file list, a side-by-side
diff, amend, discard, an optional message from Claude Code or Codex), the history with a
branch graph and search, branches with a merge preview, fetch, pull and push with HTTPS and
SSH sign-in, and cloning.

![Omagit full width](https://raw.githubusercontent.com/liri2006/omagit/main/docs/screenshots/commit.png)

![Omagit as a half tile and as two 470 px tiles](https://raw.githubusercontent.com/liri2006/omagit/main/docs/screenshots/tiles.png)

## Package

- Upstream: https://github.com/liri2006/omagit, v0.2.0, MIT, `x86_64` and `aarch64`.
- Builds the tagged source with `qmake6` and makepkg's `CFLAGS`, `CXXFLAGS` and
  `LDFLAGS`, and installs with `make INSTALL_ROOT="$pkgdir" install`: the binary, the
  desktop entry, the icon, the licence and the README.
- `CONFIG+=no_screenshot_keys` leaves out a test-only option, the only user of Qt's
  private API, so the binary is built on Qt's public API alone and does not need a
  rebuild for every `qt6-base` update.
- Runtime dependencies are `qt6-base`, `git` and `ttf-font-nerd`, which the Omarchy font
  provides. Optional: `openssh`, `libsecret` (remembered sign-ins through git's libsecret
  helper), `github-cli` (clone from your GitHub list), `nautilus-python`, `claude-code`,
  `openai-codex`.
- No install hooks, services or root helpers. It writes nothing into a home directory; the
  optional "Open in Omagit" Nautilus entry is added from the app's Settings when the user
  asks for it.
- `check()` runs the three test suites offscreen with a throw-away config and runtime
  directory: the git wrapper, the merge preview and the widgets. That is 240 checks, about
  three minutes.
- `.omarchy/package.json` follows GitHub releases. Edge only; I left the release ring to you.

## Tested

- `makepkg -f` on Omarchy 4.0.4 (qt6-base 6.11.2, makepkg's default flags including
  LTO) builds the package, and `check()` passes all 240 checks. The package holds
  `/usr/bin/omagit`, the desktop entry, the icon, `LICENSE` and `README.md`.
- The binary is a PIE with full RELRO, BIND_NOW and FORTIFY, and has no
  `Qt_6_PRIVATE_API` symbol references (`objdump -T`). A screenshot it renders is
  pixel-identical to one from a development build.
- `desktop-file-validate` passes on the desktop entry.
- `validate_package_metadata pkgbuilds/omagit` passes, and
  `bin/repo build --local --dry-run --package omagit` plans `omagit` 0.2.0-1 for edge
  x86_64.
- TODO: `python helpers/upstream-watch.py check pkgbuilds/omagit` (needs the GitHub release)
- TODO: `namcap PKGBUILD omagit-0.2.0-1-x86_64.pkg.tar.zst`
