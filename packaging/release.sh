#!/usr/bin/env bash
# Point the package recipes at a release that is on GitHub:
#   packaging/release.sh 0.9.0
# Downloads the v<version> tarball GitHub serves, writes pkgver, pkgrel and
# sha256sums into the release recipes, and regenerates the AUR .SRCINFO
# files. Then it builds the package from aur/omagit and attaches it to the
# GitHub release as omagit-<arch>.pkg.tar.zst, the file the README installs.
# It pushes nothing to the AUR; see packaging/README.md for the steps after.
set -euo pipefail
cd "$(dirname "$0")"

version=${1:-}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: packaging/release.sh <version, e.g. 0.9.0>" >&2; exit 2; }

built=$(grep -o 'OMAGIT_VERSION=[^ ]*' ../omagit.pro | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
[[ $built == "$version" ]] || { echo "omagit.pro builds $built, not $version: set OMAGIT_VERSION first" >&2; exit 1; }
commit=$(git -C .. rev-parse --short=7 "v$version^{commit}" 2>/dev/null) \
  || { echo "no tag v$version here: tag the release first" >&2; exit 1; }
gh release view "v$version" -R liri2006/omagit >/dev/null \
  || { echo "no GitHub release v$version: gh release create it first" >&2; exit 1; }

url="https://github.com/liri2006/omagit/archive/refs/tags/v$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/source.tar.gz" "$url" \
  || { echo "cannot download $url: is the tag pushed and the repository public?" >&2; exit 1; }
sum=$(sha256sum "$tmp/source.tar.gz" | cut -d' ' -f1)

for recipe in aur/omagit/PKGBUILD omarchy-pkgs/omagit/PKGBUILD; do
  sed -i -e "s/^pkgver=.*/pkgver=$version/" \
         -e "s/^pkgrel=.*/pkgrel=1/" \
         -e "s/^sha256sums=.*/sha256sums=('$sum')/" "$recipe"
done
# The -git recipe works its version out when it builds; this is only what
# the AUR shows until then.
sed -i "s/^pkgver=.*/pkgver=$version.r0.g$commit/" aur/omagit-git/PKGBUILD

for dir in aur/omagit aur/omagit-git; do
  (cd "$dir" && makepkg --printsrcinfo > .SRCINFO)
done

# Same name in every release, so releases/latest/download/<name> finds the newest.
(cd aur/omagit && makepkg -fsc)
asset="$tmp/omagit-$(uname -m).pkg.tar.zst"
cp "$(cd aur/omagit && makepkg --packagelist)" "$asset"
gh release upload "v$version" "$asset" --clobber -R liri2006/omagit

echo "v$version ($commit): sha256 $sum, $(basename "$asset") attached to the release"
echo "Next: commit the recipe changes, then publish them (packaging/README.md)."
