#!/usr/bin/env bash
# Download elementary icon theme (with cursors) into $ROOT/data/icons/elementary.
# Source: https://github.com/elementary/icons
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/data/icons/elementary"
CACHE="$ROOT/.cache/elementary-icons"

quiet="${MESON_INSTALL_QUIET:-}"
log() {
  [[ "$quiet" == 1 ]] || echo "$@"
}

ensure_icons() {
  if [[ -f "$DEST/index.theme" && ! -L "$DEST" ]]; then
    log "elementary icons already present at $DEST"
    return
  fi

  if [[ -L "$DEST" ]]; then
    log "Removing symlink; installing a local copy of elementary icons"
    rm -f "$DEST"
  fi

  mkdir -p "$ROOT/data/icons"

  if command -v rsvg-convert >/dev/null && command -v xcursorgen >/dev/null; then
    log "Building elementary icons from GitHub..."
    if [[ ! -d "$CACHE/.git" ]]; then
      git clone --depth 1 https://github.com/elementary/icons.git "$CACHE"
    else
      git -C "$CACHE" pull --ff-only
    fi
    BUILD="$ROOT/build-elementary-icons"
    meson setup "$BUILD" "$CACHE" --prefix="$ROOT/data" --datadir="$ROOT/data" -Dscale_factors=1,2
    ninja -C "$BUILD"
    ninja -C "$BUILD" install
    log "Installed elementary icons to $DEST"
    return
  fi

  log "Downloading elementary-icon-theme package..."
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  cd "$TMP"
  apt download elementary-icon-theme
  DEB="$(echo elementary-icon-theme_*.deb)"
  dpkg-deb -x "$DEB" extracted
  rm -rf "$DEST"
  cp -a extracted/usr/share/icons/elementary "$DEST"
  log "Installed elementary icons to $DEST"
}

if [[ "${1:-}" == "--install" ]]; then
  if [[ $# -ne 2 ]]; then
    echo "usage: $0 --install <relative-install-path>" >&2
    exit 2
  fi
  if [[ -z "${MESON_INSTALL_DESTDIR_PREFIX:-}" ]]; then
    echo "MESON_INSTALL_DESTDIR_PREFIX is required for --install" >&2
    exit 2
  fi

  INSTALL_DEST="$MESON_INSTALL_DESTDIR_PREFIX/$2"
  ensure_icons
  rm -rf "$INSTALL_DEST"
  mkdir -p "$INSTALL_DEST"
  cp -a "$DEST"/. "$INSTALL_DEST"/
  log "Installed elementary icons to $INSTALL_DEST"
  exit 0
fi

STAMP="${1:-}"
ensure_icons
[[ -n "$STAMP" ]] && touch "$STAMP"
