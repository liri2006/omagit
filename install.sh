#!/usr/bin/env bash
# Build Omagit and install it for the current user:
#   ~/.local/bin/omagit, a desktop entry and an icon.
# Nautilus's "Open in Omagit" entry is added from the app: Settings (Ctrl+,).
set -euo pipefail
cd "$(dirname "$0")"

command -v qmake6 >/dev/null || { echo "qmake6 not found — install qt6-base"; exit 1; }

qmake6 CONFIG+=release omagit.pro
make -j"$(nproc)"

install -Dm755 omagit "$HOME/.local/bin/omagit"
install -Dm644 data/omagit.desktop "$HOME/.local/share/applications/omagit.desktop"
install -Dm644 data/omagit.svg "$HOME/.local/share/icons/hicolor/scalable/apps/omagit.svg"

update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo "Installed. Run: omagit [path]"
