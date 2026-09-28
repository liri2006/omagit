# Add Omagit

<!-- Draft description for the pull request to omacom/omarchy-pkgs.
     Fill in the TODOs before opening it. -->

Adds `omagit`, [Omagit](https://github.com/liri2006/omagit) 0.9.0: an Omarchy-native git
GUI designed for tiling window managers.

Omagit stays usable in a tiling layout for as long as possible. As its tile shrinks,
labels turn into icons and less-used buttons move into a menu. In a narrow tile, it shows
one thing at a time: your changes, the diff or the history.

It follows the active Omarchy theme (`colors.toml`, the shell font and text size,
`icons.theme`) and updates live on `omarchy theme set` and `omarchy display text size`.
You can do everything from the keyboard.

It covers review and commit (tree, compact or table file list, side-by-side or unified
diff, amend, discard, an optional message from Claude Code or Codex), the history with a
branch graph and search, branches with a merge preview, pull, push and fetch with HTTPS
and SSH sign-in, and cloning.

![Omagit in four tiles of one screen](https://raw.githubusercontent.com/liri2006/omagit/main/docs/screenshots/tiles.png)

![Omagit windows opening side by side on a Hyprland screen, switching views and closing again](https://raw.githubusercontent.com/liri2006/omagit/main/docs/screenshots/tiling.webp)

## Package

- Upstream: https://github.com/liri2006/omagit, v0.9.0, MIT, `x86_64` and `aarch64`.
- Builds the tagged source with `qmake6` and makepkg's `CFLAGS`, `CXXFLAGS` and
  `LDFLAGS`, and installs with `make INSTALL_ROOT="$pkgdir" install`: the binary, the
  desktop entry, the icon, the licence and the README.
- `CONFIG+=no_screenshot_keys` leaves out a test-only option, the only user of Qt's
  private API, so the binary is built on Qt's public API alone and does not need a
  rebuild for every `qt6-base` update.
- Runtime dependencies are `qt6-base`, `qt6-svg` (the checkbox marks and the window
  icon are SVGs), `git` and `ttf-font-nerd`, which the Omarchy font provides.
  Optional: `openssh`, `libsecret` (remembered sign-ins through git's libsecret helper),
  `github-cli` (clone from your GitHub list), `nautilus-python`, `claude-code`,
  `openai-codex`.
- No install hooks, services or root helpers. The package installs nothing into home
  directories. The optional "Open in Omagit" Nautilus entry is added from the app's
  Settings when the user asks for it.
- `check()` runs the git wrapper and merge preview suites offscreen, with a throw-away
  config and runtime directory, in about half a minute. The widget suite checks pixel
  sizes against the design, which depend on fonts and timing, so it stays out of the
  package build.
- `.omarchy/package.json` follows GitHub releases, held for 24 hours
  (`min_release_age`). I'd like it on the fast release ring, like Task Manager for
  Omarchy, so releases reach stable without waiting for an Omarchy release. That's your
  call, so I left it out of the metadata.
- I'm not on `.github/VOUCHED.td`, so the build needs the `build-approved` label.

## Tested

- `makepkg -f` on Omarchy 4.0.4 (qt6-base 6.11.2, makepkg's default flags including
  LTO) builds the package, and `check()` passes. The package holds
  `/usr/bin/omagit`, the desktop entry, the icon, `LICENSE` and `README.md`.
- The binary is a PIE with full RELRO, BIND_NOW and FORTIFY, and has no
  `Qt_6_PRIVATE_API` symbol references (`objdump -T`). A screenshot it renders is
  pixel-identical to one from a development build.
- `desktop-file-validate` passes on the desktop entry.
- `check()` also passes in a clean room: a blank home, only the JetBrains Mono Nerd font,
  and no ssh, Nautilus or `gh` on PATH.
- aarch64: under QEMU on Arch Linux ARM (Qt 6.11.2), the package builds and `check()`
  passes. The widget suite passes there too.
- `validate_package_metadata pkgbuilds/omagit` passes, and
  `bin/repo build --local --dry-run --package omagit` plans `omagit` 0.9.0-1 for edge
  x86_64. `tests/pinned-sources.sh` passes.
- TODO: `python helpers/upstream-watch.py check pkgbuilds/omagit` (a day after the GitHub
  release, because of the 24-hour hold)
- TODO: `namcap PKGBUILD omagit-0.9.0-1-x86_64.pkg.tar.zst`
