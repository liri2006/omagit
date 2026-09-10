#!/usr/bin/env bash
# Build Omagit and install it for the current user:
#   ~/.local/bin/omagit, a desktop entry, an icon, and the Nautilus context-menu extension.
set -euo pipefail
cd "$(dirname "$0")"

command -v qmake6 >/dev/null || { echo "qmake6 not found — install qt6-base"; exit 1; }
python3 -c "import gi; gi.require_version('Nautilus', '4.1')" 2>/dev/null \
  || echo "note: nautilus-python is not installed; the context menu will not appear (omarchy pkg add nautilus-python)"

qmake6 CONFIG+=release omagit.pro
make -j"$(nproc)"

install -Dm755 omagit "$HOME/.local/bin/omagit"
install -Dm644 data/omagit.desktop "$HOME/.local/share/applications/omagit.desktop"
install -Dm644 data/omagit.svg "$HOME/.local/share/icons/hicolor/scalable/apps/omagit.svg"
install -Dm644 nautilus/omagit.py "$HOME/.local/share/nautilus-python/extensions/omagit.py"

update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

if [[ "${1:-}" != "--no-restart" ]] && pgrep -x nautilus >/dev/null; then
  echo "Restarting Nautilus so it loads the new extension..."
  nautilus -q || true
fi

echo "Installed. Run: omagit [path]   or right-click a folder in Nautilus → Open in Omagit"
