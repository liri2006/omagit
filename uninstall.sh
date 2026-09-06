#!/usr/bin/env bash
set -euo pipefail
rm -f "$HOME/.local/bin/omagit" \
      "$HOME/.local/share/applications/omagit.desktop" \
      "$HOME/.local/share/icons/hicolor/scalable/apps/omagit.svg" \
      "$HOME/.local/share/nautilus-python/extensions/omagit.py"
pgrep -x nautilus >/dev/null && nautilus -q || true
echo "OmaGit removed."
