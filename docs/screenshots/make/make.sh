#!/usr/bin/env bash
# Regenerates the README pictures from "tidewatch", a fictional repository:
#   tiles.png   Omagit in four tiles of one Hyprland screen, each a different layout
#   themes.png  the commit view in five Omarchy themes, cut into diagonal bands
#   tiling.webp Omagit windows opening, switching views and closing on a Hyprland
#               screen (and tiling.mp4 for posting)
# Usage: make.sh [tiles] [themes] [tiling]   (all three without arguments)
# Needs the development build in the repository root (qmake6 omagit.pro && make),
# ImageMagick, ffmpeg with libx264 and libwebp, and Omarchy's themes and fonts;
# oxipng, when installed, shrinks the PNGs without changing a pixel.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
dest=$(cd "$here/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/omagit-shots.XXXXXX")
export WORK
trap 'rm -rf "$WORK"' EXIT
"$here/demo-repo.sh" "$WORK/home" >/dev/null

# Tokyo Night's darkest background under the theme names, its default wallpaper behind
# the tiles, and Hyprland's borders on Omarchy.
BG='#0e0e14' ACTIVE='#7aa2f7' INACTIVE='rgba(89,89,89,0.667)'
WALLPAPER=/usr/share/omarchy/themes/tokyo-night/backgrounds/0-winding-road.jpg
FONT=JetBrainsMono-NF-Regular
# Turns on All branches in the history, then gives the filter its focus back.
ALL_BRANCHES='@allBranches,Space,@QLineEdit'

shot() { "$here/shot.sh" "$@"; }

# place <out> <backdrop> <file:x:y:border>… — pastes renders at their 2× client
# positions, each inside a 4 px (2 px at 1×) border of its colour.
place() {
    local out=$1 backdrop=$2 spec file x y border w h
    shift 2
    local args=("$backdrop")
    for spec in "$@"; do
        IFS=: read -r file x y border <<<"$spec"
        read -r w h < <(magick identify -format '%w %h\n' "$file")
        args+=(-fill "$border" -draw "rectangle $((x - 4)),$((y - 4)) $((x + w + 3)),$((y + h + 3))"
            "$file" -geometry "+$x+$y" -composite)
    done
    magick "${args[@]}" "$out"
}

# optimize <png> — lossless: drops the opaque alpha channel and recompresses with
# Zopfli (a few minutes for tiles.png).
optimize() {
    if command -v oxipng >/dev/null; then
        oxipng -o max --zopfli --strip safe -a -q "$1"
    else
        echo "oxipng not found: $(basename "$1") is not optimised" >&2
    fi
}

# A 1920×1280 screen at scale 2 without its 26 px bar: a half, a quarter and two
# eighths with Omarchy's gaps (10 px outside, 2 × 5 px between) and 2 px borders,
# on the wallpaper as it fills that screen.
tiles() {
    local t=$WORK/tiles
    mkdir -p "$t"
    magick "$WALLPAPER" -resize 3840x2560^ -gravity center -extent 3840x2560 \
        -gravity northwest -crop 3840x2508+0+52 +repage "$t/wallpaper.png"
    CONF=$'[window]\nleftWidth=470\n[diff]\ntwoPane=false' \
        shot "$t/history.png" 941x1230 --history --screenshot-keys "$ALL_BRANCHES" --screenshot-after 2000 &
    shot "$t/commit.png" 941x608 --select src/render.rs --screenshot-keys Ctrl+G --screenshot-after 2500 &
    shot "$t/mini.png" 463x608 --select src/render.rs --mini --screenshot-menu commit \
        --screenshot-keys Ctrl+G --screenshot-after 1500 &
    shot "$t/branch.png" 464x608 --select src/render.rs --screenshot-menu branch --screenshot-after 1500 &
    wait
    place "$dest/tiles.png" "$t/wallpaper.png" "$t/history.png:24:24:$INACTIVE" "$t/commit.png:1934:24:$ACTIVE" \
        "$t/mini.png:1934:1268:$INACTIVE" "$t/branch.png:2888:1268:$INACTIVE"
    optimize "$dest/tiles.png"
    echo "tiles.png"
}

# The quarter-tile commit view in each theme, left to right in diagonal bands,
# the theme names in a strip underneath.
themes() {
    local t=$WORK/themes names=(tokyo-night catppuccin-latte gruvbox rose-pine osaka-jade) name
    mkdir -p "$t"
    for name in "${names[@]}"; do
        THEME=$name shot "$t/$name.png" 941x608 --select src/render.rs --screenshot-keys Ctrl+G \
            --screenshot-after 2500 &
    done
    wait
    local n=${#names[@]} W H slant=150 strip=96 i
    read -r W H < <(magick identify -format '%w %h\n' "$t/${names[0]}.png")
    local args=("$t/${names[0]}.png")
    for ((i = 1; i < n; i++)); do
        local b0=$((W * i / n)) b1=$((i == n - 1 ? W * 4 : W * (i + 1) / n))
        args+=(\( "$t/${names[i]}.png" \( -size "${W}x$H" xc:black -fill white
            -draw "polygon $((b0 + slant)),0 $((b1 + slant)),0 $((b1 - slant)),$H $((b0 - slant)),$H" \)
            -alpha off -compose CopyOpacity -composite \) -compose over -composite)
    done
    args+=(-background "$BG" -gravity north -extent "${W}x$((H + strip))" -gravity northwest
        -font "$FONT" -pointsize 26 -fill '#a9b1d6')
    for ((i = 0; i < n; i++)); do
        local left=$((i == 0 ? 0 : W * i / n - slant)) right=$((i == n - 1 ? W : W * (i + 1) / n - slant))
        local label width
        label=$(sed 's/-/ /g; s/\b\(.\)/\u\1/g' <<<"${names[i]}")
        width=$(magick -font "$FONT" -pointsize 26 label:"$label" -format %w info:)
        args+=(-annotate "+$(((left + right - width) / 2))+$((H + 30))" "$label")
    done
    magick "${args[@]}" "$dest/themes.png"
    optimize "$dest/themes.png"
    echo "themes.png"
}

tiling() {
    "$here/tiling.py" "$dest"
}

for part in "${@:-tiles themes tiling}"; do
    for p in $part; do
        case $p in
            tiles | themes | tiling) "$p" ;;
            *) echo "usage: make.sh [tiles] [themes] [tiling]" >&2; exit 2 ;;
        esac
    done
done
