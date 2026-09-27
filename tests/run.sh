#!/usr/bin/env bash
# Build and run every Omagit test suite. Each suite gets a throw-away
# XDG_CONFIG_HOME so a run never reads or writes the real omagit.conf (and a
# throw-away XDG_RUNTIME_DIR for the askpass sockets), and the
# widget suites render offscreen with the Fusion style, independent of the
# desktop's platform theme.
#   ./run.sh            # all suites
#   ./run.sh ui         # only the named ones (gitrepo, mergedialog, ui)
set -euo pipefail
cd "$(dirname "$0")"

command -v qmake6 >/dev/null || { echo "qmake6 not found — install qt6-base"; exit 1; }
jobs="$(nproc)"

# name:project file:makefile:kind (core suites need no display, widget ones do)
suites=(
  "gitrepo:tests.pro:Makefile:core"
  "mergedialog:mergedialog.pro:Makefile.mergedialog:widget"
  "ui:ui.pro:Makefile.ui:widget"
)

wanted=("$@")
selected() {
  [[ ${#wanted[@]} -eq 0 ]] && return 0
  local name
  for name in "${wanted[@]}"; do [[ "$name" == "$1" ]] && return 0; done
  return 1
}

status=0
for suite in "${suites[@]}"; do
  IFS=: read -r name project makefile kind <<<"$suite"
  selected "$name" || continue
  echo "== $name"
  qmake6 "$project" -o "$makefile" >/dev/null
  make -j"$jobs" -f "$makefile" >/dev/null
  # A runtime directory of their own too: the askpass tests listen and clear
  # away sockets there, and must never touch a running Omagit's.
  env=(XDG_CONFIG_HOME="$(mktemp -d)" XDG_RUNTIME_DIR="$(mktemp -d)")
  if [[ "$kind" == widget ]]; then
    env+=(QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion)
  fi
  env "${env[@]}" "../build/tests/${name}_test" || status=1
done
exit $status
