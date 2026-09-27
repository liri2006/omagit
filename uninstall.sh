#!/usr/bin/env bash
set -euo pipefail
rm -f "$HOME/.local/bin/omagit" \
      "$HOME/.local/share/applications/omagit.desktop" \
      "$HOME/.local/share/icons/hicolor/scalable/apps/omagit.svg"
# The Nautilus entry, if Settings added it; a running Nautilus keeps it until it quits.
extension="${XDG_DATA_HOME:-$HOME/.local/share}/nautilus-python/extensions/omagit.py"
if [[ -e "$extension" ]]; then
  rm -f "$extension"
  pgrep -x nautilus >/dev/null && nautilus -q || true
fi
echo "Omagit removed."
