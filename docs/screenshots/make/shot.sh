#!/usr/bin/env bash
# shot.sh <out.png> <w>x<h> [omagit args…] — one offscreen render of the demo
# repository in $WORK/home (built by demo-repo.sh), at 2×, in a clean profile.
# THEME picks the Omarchy theme (default tokyo-night), CONF adds lines to
# omagit.conf. Needs the development build in the repository root, because
# release builds leave out --screenshot-keys.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$1 size=$2; shift 2
: "${WORK:?WORK must name the folder demo-repo.sh built into}"
run=$(mktemp -d "$WORK/run.XXXXXX")
mkdir -p "$run/config/omagit" "$run/config/omarchy" "$run/runtime" "$run/data" "$run/state"
chmod 700 "$run/runtime"
printf '[font]\nbase-size = 12\n' > "$run/config/omarchy/shell.toml"
printf '[agent]\nname=claude\n%s\n' "${CONF:-}" > "$run/config/omagit/omagit.conf"
cd "$WORK/home/Projects/tidewatch"
env -i PATH="$here/fake-bin:/usr/bin" HOME="$WORK/home" LANG=en_US.UTF-8 \
    XDG_CONFIG_HOME="$run/config" XDG_DATA_HOME="$run/data" XDG_CACHE_HOME="$WORK/cache" \
    XDG_STATE_HOME="$run/state" XDG_RUNTIME_DIR="$run/runtime" \
    GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 \
    QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 \
    OMAGIT_THEME_DIR="/usr/share/omarchy/themes/${THEME:-tokyo-night}" \
    "$root/omagit" --no-fetch --screenshot "$out" --screenshot-size "$size" "$@" \
    2> >(grep -v -E "^$|propagateSizeHints|QFont::setPointSize" >&2)
rm -rf "$run"
