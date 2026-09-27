#!/usr/bin/env bash
# Build Omagit and install it for the current user:
#   ~/.local/bin/omagit, a desktop entry and an icon.
# Nautilus's "Open in Omagit" entry is added from the app: Settings (Ctrl+,).
# A packaged Omagit (/usr/bin/omagit) is shadowed by this copy while it exists;
# ./uninstall.sh removes it again.
set -euo pipefail
cd "$(dirname "$0")"

command -v qmake6 >/dev/null || { echo "qmake6 not found — install qt6-base"; exit 1; }

qmake6 CONFIG+=release PREFIX="$HOME/.local" omagit.pro
make -j"$(nproc)"
make install

update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo "Installed. Run: omagit [path]"
