# Packaging

Recipes for the two places Omarchy users install software from. Nothing here is
published by itself; the steps below say what to run when.

| Recipe | Becomes | Builds from |
|--------|---------|-------------|
| `aur/omagit/` | the AUR package `omagit` | the tagged release's source tarball |
| `aur/omagit-git/` | the AUR package `omagit-git` | the latest commit on `main` |
| `omarchy-pkgs/omagit/` | `pkgbuilds/omagit/` in [omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs), the source of the Omarchy package repository | the tagged release's source tarball; `.omarchy/package.json` lets their bot pick up new GitHub releases |

All three:

- build with `qmake6 CONFIG+=no_screenshot_keys PREFIX=/usr` and makepkg's compiler
  and linker flags, and install with `make INSTALL_ROOT="$pkgdir" install`. The switch
  leaves out the test-only `--screenshot-keys`, the only user of Qt's private API, so
  the binary uses Qt's public API alone and keeps working across `qt6-base` updates;
- run the git wrapper and merge preview suites in `check()` (`tests/run.sh gitrepo
  mergedialog`: offscreen, with a throw-away config and runtime directory; about half a
  minute). The widget suite checks pixel sizes against the design, which depend on fonts
  and timing, so it runs in development only;
- install `/usr/bin/omagit`, the desktop entry, the icon, `LICENSE` and `README.md`,
  and nothing in a home directory. The Nautilus entry is added from the app's Settings,
  only when asked for.

The release tarball leaves out `design/`, `docs/`, `.claude/` and `packaging/`
(`.gitattributes`), which takes it from about 14 MB to about 0.5 MB.

## Once, before the first release

1. Publish the repository; every recipe downloads from it. The history was rewritten
   on 2026-09-27 (noreply email, no agent notes), so publish it as a fresh repository
   rather than force-pushing and making the private one public — GitHub keeps the old
   commits reachable by their hashes. Rename (or delete) the private one first:
   ```bash
   gh repo rename omagit-private -R liri2006/omagit
   git remote remove origin
   gh repo create liri2006/omagit --public --source . --remote origin --push
   git config user.email 2411723+liri2006@users.noreply.github.com   # future commits too
   ```
2. Give the GitHub repository its description and topics:
   ```bash
   gh repo edit liri2006/omagit \
     --description "An Omarchy-native git GUI designed for tiling window managers." \
     --add-topic omarchy --add-topic hyprland --add-topic tiling-window-manager \
     --add-topic git --add-topic git-gui --add-topic qt6
   ```
3. An AUR account with your SSH public key: <https://aur.archlinux.org/account>.

## Releasing a version

1. Set `OMAGIT_VERSION` in `omagit.pro` and commit.
2. Tag and push:
   ```bash
   git tag -a v0.9.0 -m "Omagit 0.9.0"
   git push origin main v0.9.0
   ```
3. Make a GitHub release for the tag. The Omarchy repository's upstream watch reads
   GitHub *releases*, not bare tags:
   ```bash
   gh release create v0.9.0 --title "Omagit 0.9.0" --generate-notes
   ```
4. Point the recipes at it, then build the result once:
   ```bash
   packaging/release.sh 0.9.0            # checksums, pkgver, .SRCINFO
   (cd packaging/aur/omagit && makepkg -fsc && namcap PKGBUILD *.pkg.tar.zst)
   ```
5. Commit the recipe changes.

## Publishing to the AUR

The first time, each package's AUR repository is created by pushing to it:

```bash
git clone ssh://aur@aur.archlinux.org/omagit.git /tmp/aur-omagit
cp packaging/aur/omagit/{PKGBUILD,.SRCINFO} /tmp/aur-omagit/
cd /tmp/aur-omagit && git add PKGBUILD .SRCINFO && git commit -m "omagit 0.9.0" && git push
```

`omagit-git` works the same way (`ssh://aur@aur.archlinux.org/omagit-git.git`); it only
needs a push when its recipe changes, not for every commit. Later releases of `omagit`:
run steps 1–5 above, copy the two files over, commit and push.

## Submitting to the Omarchy package repository

The Omarchy package repository (`pkgs.omarchy.org`, the `[omarchy]` section of
`/etc/pacman.conf`) is built from [omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs).
Anyone can open a pull request; a maintainer approves the build (the `build-approved`
label), merges it, and their CI builds, signs and publishes the package, on the `edge`
channel first. Accepted outside apps look like this one: Meeting Recorder (#619), Task
Manager for Omarchy (#579).

1. Fork and branch:
   ```bash
   gh repo fork omacom/omarchy-pkgs --clone && cd omarchy-pkgs
   git switch -c add-omagit
   cp -r ../omagit/packaging/omarchy-pkgs/omagit pkgbuilds/omagit
   ```
2. Check it with their tools:
   ```bash
   python helpers/upstream-watch.py check pkgbuilds/omagit   # a day after the release (24 h hold)
   bin/repo build --local --dry-run --package omagit
   ```
3. Commit (`Add Omagit`), push, and open the pull request with
   [`omarchy-pkgs/PULL_REQUEST.md`](omarchy-pkgs/PULL_REQUEST.md) as its description,
   filling in the test results and a screenshot.

Once it is merged, `omarchy pkg add omagit` installs it; add that line to the README's
Install section then. Being in the package repository does not preinstall it or put it in
the Install menu. Those would be separate pull requests to
[omacom/omarchy](https://github.com/omacom/omarchy) and are up to its maintainers.
