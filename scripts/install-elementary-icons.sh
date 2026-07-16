#!/usr/bin/env bash
# Install elementary icon theme (with cursors) into $ROOT/data/icons/elementary.
#
# Prefer the vendored archive (no network). Fall back to building from GitHub
# or apt only if the archive is missing.
# Source upstream: https://github.com/elementary/icons
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/data/icons/elementary"
ARCHIVE="$ROOT/data/icons/elementary-icons.tar.xz"
CACHE="$ROOT/.cache/elementary-icons"

quiet="${MESON_INSTALL_QUIET:-}"
log() {
  [[ "$quiet" == 1 ]] || echo "$@"
}

# Upstream elementary dropped many freedesktop names/symlinks; inherit the
# maintained elementary-xfce set (deb Recommends) so lookups fall through to
# it instead of hicolor/symbolic.
ensure_inherits() {
  local index="$DEST/index.theme"

  [[ -f "$index" ]] || return 0
  if grep -q '^Inherits=' "$index"; then
    sed -i 's/^Inherits=.*/Inherits=elementary-xfce,hicolor/' "$index"
  else
    sed -i '/^\[Icon Theme\]/a Inherits=elementary-xfce,hicolor' "$index"
  fi
}

# GTK >= 4.20 renders simple symbolic SVGs with its own path renderer, which
# ignores elementary's sheet-export root translate and draws them blank.
fix_symbolic() {
  python3 "$ROOT/scripts/fix-symbolic-svgs.py" "$DEST"
}

normalize_theme() {
  ensure_inherits
  fix_symbolic
}

ensure_icons() {
  if [[ -f "$DEST/index.theme" && ! -L "$DEST" ]]; then
    log "elementary icons already present at $DEST"
    normalize_theme
    return
  fi

  if [[ -L "$DEST" ]]; then
    log "Removing symlink; installing a local copy of elementary icons"
    rm -f "$DEST"
  fi

  mkdir -p "$ROOT/data/icons"

  if [[ -f "$ARCHIVE" ]]; then
    log "Extracting vendored elementary icons from $ARCHIVE"
    rm -rf "$DEST"
    tar -C "$ROOT/data/icons" -xJf "$ARCHIVE"
    if [[ ! -f "$DEST/index.theme" ]]; then
      echo "error: archive did not produce $DEST/index.theme" >&2
      exit 1
    fi
    normalize_theme
    log "Installed elementary icons to $DEST"
    return
  fi

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
    normalize_theme
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
  normalize_theme
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
if [[ -n "$STAMP" ]]; then
  touch "$STAMP"
fi
