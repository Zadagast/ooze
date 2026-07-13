#!/usr/bin/env bash
# Capture first-party Ooze app windows into site/assets/apps/ for the project website.
#
# These are standalone GTK4 windows (Ooze Gel) on the host X11 display — not a full
# nested desktop. Nest/desktop light+dark shots still come from docs/ / manual nest
# capture (GNOME portal blocks non-interactive host screenshots).
#
# Usage (from repo root, after ninja -C build):
#   ./scripts/capture-site-screenshots.sh
#
# Requires: ImageMagick `import`, `xwininfo`, built binaries under build/.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export DISPLAY="${DISPLAY:-:0}"
unset WAYLAND_DISPLAY || true
export GDK_BACKEND=x11
export PATH="$ROOT/build:$PATH"
export OOZE_DATA_DIR="$ROOT/data"
export XDG_DATA_DIRS="$ROOT/data${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export XDG_CONFIG_HOME="${OOZE_XDG_CONFIG:-$ROOT/data/xdg-config}"
export GSETTINGS_BACKEND=keyfile
export OOZE_THEMES_DIR="${OOZE_THEMES_DIR:-$ROOT/data/themes}"

OUT="$ROOT/site/assets/apps"
mkdir -p "$OUT" "$XDG_CONFIG_HOME"

need() { command -v "$1" >/dev/null || { echo "missing: $1"; exit 1; }; }
need import
need xwininfo

capture_app() {
  local bin="$1" name="$2" title_pat="$3" outfile="$4" extra_args="${5:-}"
  local log="/tmp/ooze-site-cap-${name}.log"
  local base
  base="$(basename "$bin")"
  pkill -x "$base" 2>/dev/null || true
  sleep 0.2
  # shellcheck disable=SC2086
  timeout 25 "$bin" $extra_args >"$log" 2>&1 &
  local pid=$!
  local win="" tmp="/tmp/ooze-site-cap-${name}.png"
  rm -f "$tmp"
  for _ in $(seq 1 50); do
    win=$(xwininfo -root -tree 2>/dev/null | rg -i "$title_pat" | rg -v '1x1\+' | head -1 | awk '{print $1}' || true)
    if [[ -n "$win" ]]; then
      sleep 0.7
      if import -window "$win" "$tmp" 2>/dev/null; then
        break
      fi
    fi
    sleep 0.2
  done
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  if [[ ! -f "$tmp" ]]; then
    echo "FAIL $name — see $log"
    return 1
  fi
  if command -v convert >/dev/null; then
    convert "$tmp" -strip "$OUT/$outfile"
  else
    cp "$tmp" "$OUT/$outfile"
  fi
  echo "OK  $name -> $OUT/$outfile ($(file -b "$OUT/$outfile"))"
}

echo "Capturing Ooze apps into $OUT ..."
capture_app "$ROOT/build/spot" spot 'Spot|"efrosch"|Home|Documents' spot.png
capture_app "$ROOT/build/ooze-command" command 'Ooze Command|ooze-command|Terminal' command.png
capture_app "$ROOT/build/ooze-eye" eye 'Eye|ooze-eye|ooze-splat|\.png' eye.png "$ROOT/site/assets/ooze-splat.png"
capture_app "$ROOT/build/ooze-king" king 'King|ooze-king|System Settings|Launcher' king.png
capture_app "$ROOT/build/ooze-torrent" torrent 'Torrent|ooze-torrent' torrent.png
capture_app "$ROOT/build/ooze-monitor" monitor 'Monitor|Displays|Display Settings|ooze-monitor' monitor.png
capture_app "$ROOT/build/ooze-ear" ear 'Ear|Sound|ooze-ear' ear.png
capture_app "$ROOT/build/ooze-pak" pak 'Pak|Flatpak|Software|ooze-pak' pak.png
capture_app "$ROOT/build/ooze-about" about 'About|This Computer|ooze-about' about.png
capture_app "$ROOT/build/ooze-themes" themes 'Themes|Appearance|ooze-themes' themes.png

echo
echo "Done. Desktop overview PNGs (site/assets/ooze-{light,dark}.png) are separate:"
echo "  1. ./run-devkit.sh"
echo "  2. Arrange Spot + Command (or empty desk), toggle Appearance in Themes"
echo "  3. Interactive host screenshot of the nest window → replace ooze-light/dark.png"
echo "  (Non-interactive gnome-screenshot is blocked by the GNOME portal.)"
