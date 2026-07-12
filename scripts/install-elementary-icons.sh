#!/usr/bin/env bash
# Download elementary icon theme (with cursors) into $ROOT/data/icons/elementary.
# Source: https://github.com/elementary/icons
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/data/icons/elementary"
CACHE="$ROOT/.cache/elementary-icons"
STAMP="${1:-}"

if [[ -f "$DEST/index.theme" && ! -L "$DEST" ]]; then
  [[ -n "$STAMP" ]] && touch "$STAMP"
  echo "elementary icons already present at $DEST"
  exit 0
fi

if [[ -L "$DEST" ]]; then
  echo "Removing symlink; installing a local copy of elementary icons"
  rm -f "$DEST"
fi

mkdir -p "$ROOT/data/icons"

if command -v rsvg-convert >/dev/null && command -v xcursorgen >/dev/null; then
  echo "Building elementary icons from GitHub..."
  if [[ ! -d "$CACHE/.git" ]]; then
    git clone --depth 1 https://github.com/elementary/icons.git "$CACHE"
  else
    git -C "$CACHE" pull --ff-only
  fi
  BUILD="$ROOT/build-elementary-icons"
  meson setup "$BUILD" "$CACHE" --prefix="$ROOT/data" --datadir="$ROOT/data" -Dscale_factors=1,2
  ninja -C "$BUILD"
  ninja -C "$BUILD" install
  [[ -n "$STAMP" ]] && touch "$STAMP"
  echo "Installed elementary icons to $DEST"
  exit 0
fi

echo "Downloading elementary-icon-theme package..."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
apt download elementary-icon-theme
DEB="$(echo elementary-icon-theme_*.deb)"
dpkg-deb -x "$DEB" extracted
rm -rf "$DEST"
cp -a extracted/usr/share/icons/elementary "$DEST"
[[ -n "$STAMP" ]] && touch "$STAMP"
echo "Installed elementary icons to $DEST"
