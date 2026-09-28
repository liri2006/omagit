#!/usr/bin/env python3
"""The tiling animation: Omagit windows open one after another on a Hyprland screen
(dwindle layout, Omarchy's gaps, borders and animation curves), the narrow ones
switch views, and they close again. Writes tiling.mp4 and tiling.webp to <dest>.

    tiling.py <dest>    (WORK must hold the demo repository, as for shot.sh)

Each window's view is rendered once per size at 2× by shot.sh. During a move a
window's new render takes over from its old one within 0.1 s, drawn into the
moving box as Hyprland draws a window being resized;
new windows pop in from 87 % and fade in, closed ones shrink away and fade out.
"""
import hashlib
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEST = Path(sys.argv[1])
WORK = Path(os.environ["WORK"]) / "tiling"
FPS = 30
SCALE = 1.2                        # a 1600×900 screen drawn on 1920×1080
CANVAS = (1920, 1080)
BORDER = 3                         # 2 px at the screen's scale
ACTIVE = (0x7A, 0xA2, 0xF7, 1.0)   # Tokyo Night's active border
INACTIVE = (89, 89, 89, 0.667)     # Omarchy's inactive border
WALLPAPER = "/usr/share/omarchy/themes/tokyo-night/backgrounds/0-winding-road.jpg"
ALL_BRANCHES = "@allBranches,Space,@QLineEdit"

# Hyprland's curves (Omarchy's looknfeel) and durations in seconds.
def bezier(p1x, p1y, p2x, p2y):
    def at(t, a, b):
        return 3 * (1 - t) ** 2 * t * a + 3 * (1 - t) * t * t * b + t ** 3
    def curve(x):
        lo, hi = 0.0, 1.0
        for _ in range(40):
            mid = (lo + hi) / 2
            if at(mid, p1x, p2x) < x:
                lo = mid
            else:
                hi = mid
        return at((lo + hi) / 2, p1y, p2y)
    return curve

EASE_OUT_QUINT = bezier(0.23, 1, 0.32, 1)
ALMOST_LINEAR = bezier(0.5, 0.5, 0.75, 1.0)
LINEAR = lambda x: x
MOVE, POP_IN, FADE_IN, POP_OUT, FADE_OUT, BORDER_FADE = 0.379, 0.41, 0.173, 0.149, 0.146, 0.539
SWAP = 0.1  # a moved window's new layout takes over from the old one this fast

# ── the layout: a 1600×900 screen, 10 px outer gaps, 2 px borders, 10 px between ──
FULL = (12, 12, 1576, 876)
LEFT, RIGHT = (12, 12, 781, 876), (807, 12, 781, 876)
TOP_RIGHT, BOTTOM_RIGHT = (807, 12, 781, 431), (807, 457, 781, 431)
EIGHTH_LEFT, EIGHTH_RIGHT = (807, 457, 383, 431), (1204, 457, 384, 431)

# ── the views: name → (logical size, omagit arguments) ──
COMMIT = ["--select", "src/render.rs", "--screenshot-keys", "Ctrl+G", "--screenshot-after", "1800"]
MINI = ["--select", "src/render.rs", "--mini", "--screenshot-keys", "Ctrl+G", "--screenshot-after", "1800"]
POPOVER = ["--select", "src/render.rs", "--mini", "--screenshot-menu", "commit",
           "--screenshot-keys", "Ctrl+G", "--screenshot-after", "1500"]
HISTORY = ["--history", "--screenshot-keys", ALL_BRANCHES, "--screenshot-after", "1800"]
UNIFIED = "[diff]\ntwoPane=false"  # omagit.conf lines for the one-column diff
VIEWS = {
    "a-commit-full": (FULL, COMMIT),
    "a-commit-half": (LEFT, COMMIT),
    "a-mini-half": (LEFT, MINI),
    "b-history-half": (RIGHT, HISTORY, UNIFIED),
    "b-history-quarter": (TOP_RIGHT, HISTORY, UNIFIED),
    "c-mini-quarter": (BOTTOM_RIGHT, MINI, UNIFIED),
    "c-popover-quarter": (BOTTOM_RIGHT, POPOVER, UNIFIED),
    "c-popover-eighth": (EIGHTH_LEFT, POPOVER),
    "c-changes-eighth": (EIGHTH_LEFT, COMMIT),
    "d-changes-eighth": (EIGHTH_RIGHT, COMMIT),
    "d-diff-eighth": (EIGHTH_RIGHT, MINI),
    "d-history-eighth": (EIGHTH_RIGHT, HISTORY),
}

# ── the story: at each time the focused window and either the whole layout
# (every open window's box and view: a window missing from it closes, a new one
# opens) or one window's new view in the same box ──
STEPS = [
    (0.0, "a", "layout", {"a": (FULL, "a-commit-full")}),
    (1.4, "b", "layout", {"a": (LEFT, "a-commit-half"), "b": (RIGHT, "b-history-half")}),
    (3.4, "c", "layout", {"a": (LEFT, "a-commit-half"), "b": (TOP_RIGHT, "b-history-quarter"),
                          "c": (BOTTOM_RIGHT, "c-mini-quarter")}),
    (4.5, "c", "view", {"c": (BOTTOM_RIGHT, "c-popover-quarter")}),
    (6.2, "d", "layout", {"a": (LEFT, "a-commit-half"), "b": (TOP_RIGHT, "b-history-quarter"),
                          "c": (EIGHTH_LEFT, "c-popover-eighth"), "d": (EIGHTH_RIGHT, "d-changes-eighth")}),
    (7.6, "d", "view", {"d": (EIGHTH_RIGHT, "d-diff-eighth")}),
    (8.6, "d", "view", {"d": (EIGHTH_RIGHT, "d-history-eighth")}),
    (9.7, "c", "view", {}),
    (10.1, "c", "view", {"c": (EIGHTH_LEFT, "c-changes-eighth")}),
    (11.1, "a", "view", {}),
    (11.5, "a", "view", {"a": (LEFT, "a-mini-half")}),
    (12.6, "a", "view", {"a": (LEFT, "a-commit-half")}),
    (13.4, "c", "layout", {"a": (LEFT, "a-commit-half"), "b": (TOP_RIGHT, "b-history-quarter"),
                           "c": (BOTTOM_RIGHT, "c-mini-quarter")}),
    (14.6, "b", "layout", {"a": (LEFT, "a-commit-half"), "b": (RIGHT, "b-history-half")}),
    (15.8, "a", "layout", {"a": (FULL, "a-commit-full")}),
]
END = 17.4
ORDER = "abcd"


def canvas_rect(r):
    x, y, w, h = r
    x0, y0 = round(x * SCALE), round(y * SCALE)
    return (x0, y0, round((x + w) * SCALE) - x0, round((y + h) * SCALE) - y0)


def render(name):
    rect, args, *conf = VIEWS[name]
    raw, fitted = WORK / "raw" / f"{name}.png", WORK / "fit" / f"{name}.png"
    if fitted.exists():
        return
    subprocess.run([str(HERE / "shot.sh"), str(raw), f"{rect[2]}x{rect[3]}", *args], check=True,
                   env={**os.environ, "CONF": conf[0] if conf else ""})
    _, _, w, h = canvas_rect(rect)
    subprocess.run(["magick", raw, "-filter", "Lanczos", "-resize", f"{w}x{h}!", fitted], check=True)


def states():
    """The full state after each step: {window: (rect, view)}, focus."""
    out, current = [], {}
    for t, focus, kind, change in STEPS:
        current = dict(change) if kind == "layout" else {**current, **change}
        out.append((t, focus, current))
    return out


def lerp(a, b, p):
    return a + (b - a) * p


def frame_at(t, timeline):
    """What to draw at time t: a list of (view, alpha, rect, border rgba) layers."""
    i = max(k for k, s in enumerate(timeline) if s[0] <= t)
    t0, focus, now = timeline[i]
    prev_focus, before = (timeline[i - 1][1], timeline[i - 1][2]) if i else (focus, now)
    dt = t - t0
    layers = []
    for win in ORDER:
        if win in now:
            rect, view = now[win]
            if win in before:
                old_rect, old_view = before[win]
                moved = old_rect != rect
                p = EASE_OUT_QUINT(min(dt / MOVE, 1)) if moved else 1
                box = tuple(lerp(o, n, p) for o, n in zip(old_rect, rect))
                swap = min(dt / SWAP, 1)
                parts = [(old_view, 1.0), (view, swap)] if moved and old_view != view and swap < 1 else [(view, 1.0)]
                alpha = 1.0
            else:  # opening: pop in from 87 % and fade in
                p = EASE_OUT_QUINT(min(dt / POP_IN, 1))
                s = lerp(0.87, 1, p)
                x, y, w, h = rect
                box = (x + w * (1 - s) / 2, y + h * (1 - s) / 2, w * s, h * s)
                parts, alpha = [(view, 1.0)], ALMOST_LINEAR(min(dt / FADE_IN, 1))
        elif win in before and dt < max(POP_OUT, FADE_OUT):  # closing
            rect, view = before[win]
            s = lerp(1, 0.87, LINEAR(min(dt / POP_OUT, 1)))
            x, y, w, h = rect
            box = (x + w * (1 - s) / 2, y + h * (1 - s) / 2, w * s, h * s)
            parts, alpha = [(view, 1.0)], 1 - ALMOST_LINEAR(min(dt / FADE_OUT, 1))
        else:
            continue
        was, now_on = win == prev_focus, win == focus
        q = EASE_OUT_QUINT(min(dt / BORDER_FADE, 1)) if was != now_on else 1
        a, b = (ACTIVE if was else INACTIVE), (ACTIVE if now_on else INACTIVE)
        border = tuple(lerp(x, y, q) for x, y in zip(a, b))
        layers.append((parts, alpha, canvas_rect(box), border))
    return layers


def draw(key_layers):
    key, layers = key_layers
    out = WORK / "frames" / f"{key}.png"
    if out.exists():
        return
    args = ["magick", str(WORK / "wallpaper.png")]
    for parts, alpha, (x, y, w, h), (r, g, b, a) in layers:
        if w <= 0 or h <= 0:
            continue
        args += ["-fill", "none", "-stroke", f"rgba({r:.0f},{g:.0f},{b:.0f},{a * alpha:.3f})",
                 "-strokewidth", str(BORDER),
                 "-draw", f"rectangle {x - 1.5},{y - 1.5} {x + w + 0.5},{y + h + 0.5}", "-stroke", "none"]
        for view, part_alpha in parts:
            args += ["(", str(WORK / "fit" / f"{view}.png"), "-resize", f"{w}x{h}!"]
            if alpha * part_alpha < 1:
                args += ["-alpha", "set", "-channel", "A", "-evaluate", "multiply",
                         f"{alpha * part_alpha:.3f}", "+channel"]
            args += [")", "-geometry", f"+{x}+{y}", "-composite"]
    subprocess.run(args + [str(out)], check=True)


def main():
    for d in ("raw", "fit", "frames", "seq"):
        (WORK / d).mkdir(parents=True, exist_ok=True)
    for stale in (WORK / "frames").iterdir():  # named after the story, not the renders
        stale.unlink()
    subprocess.run(["magick", WALLPAPER, "-resize", f"{CANVAS[0]}x{CANVAS[1]}^", "-gravity", "center",
                    "-extent", f"{CANVAS[0]}x{CANVAS[1]}", str(WORK / "wallpaper.png")], check=True)
    with ThreadPoolExecutor(os.cpu_count()) as pool:
        list(pool.map(render, VIEWS))

    timeline = states()
    frames, unique = [], {}
    for n in range(round(END * FPS)):
        layers = frame_at(n / FPS, timeline)
        key = hashlib.sha1(repr(layers).encode()).hexdigest()[:16]
        unique[key] = layers
        frames.append(key)
    with ThreadPoolExecutor(os.cpu_count()) as pool:
        list(pool.map(draw, unique.items()))
    for n, key in enumerate(frames):
        link = WORK / "seq" / f"{n:04d}.png"
        link.unlink(missing_ok=True)
        link.symlink_to(WORK / "frames" / f"{key}.png")

    seq = str(WORK / "seq" / "%04d.png")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", str(FPS), "-i", seq,
                    "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-tune", "animation",
                    "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(DEST / "tiling.mp4")], check=True)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", str(FPS), "-i", seq,
                    "-vf", "fps=20,scale=1280:-1:flags=lanczos", "-c:v", "libwebp_anim", "-lossless", "0",
                    "-quality", "80", "-compression_level", "6", "-loop", "0", str(DEST / "tiling.webp")],
                   check=True)
    print(f"tiling.mp4 tiling.webp ({len(frames)} frames, {len(unique)} distinct)")


main()
