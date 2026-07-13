#!/usr/bin/env bash
# Install WhiteSur GTK theme for *foreign* apps only (via GTK_THEME on launch).
#
# Usage:
#   ./scripts/install-whitesur-theme.sh
#
# Installs into the user theme path (does NOT touch ~/.config/gtk-4.0):
#   ~/.local/share/themes/WhiteSur-Light
#   ~/.local/share/themes/WhiteSur-Dark
#
# Optional (explicit): stash libadwaita CSS copies for experiments — never
# auto-linked into ~/.config/gtk-4.0 (that overrides Ooze Gel / OozeKit):
#   ./scripts/install-whitesur-theme.sh --stash-libadwaita
#
# Requires: git, sassc, glib-compile-resources (libglib2.0-dev)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${ROOT}/.cache/whitesur-gtk-theme"
REPO_URL="https://github.com/vinceliuice/WhiteSur-gtk-theme.git"

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
THEMES_DEST="${XDG_DATA_HOME}/themes"
OOZE_WS="${XDG_DATA_HOME}/ooze/whitesur"
MARKER="${OOZE_WS}/.ooze-managed"
GTK4_CONFIG="${HOME}/.config/gtk-4.0"
GTK4_LIGHT="${OOZE_WS}/gtk-4.0-light"
GTK4_DARK="${OOZE_WS}/gtk-4.0-dark"

STASH_LIBADWAITA=0
for arg in "$@"; do
  case "$arg" in
    --stash-libadwaita) STASH_LIBADWAITA=1 ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
  esac
done

quiet="${MESON_INSTALL_QUIET:-}"
log() {
  [[ "$quiet" == 1 ]] || echo "$@"
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: missing dependency '$1'" >&2
    echo "  sudo apt-get install -y git sassc libglib2.0-dev" >&2
    exit 1
  fi
}

# If a previous Ooze run left a WhiteSur gtk-4.0 symlink, remove it so Ooze
# apps get stock libadwaita / OozeKit styling again.
clear_managed_gtk4_link() {
  if [[ -L "$GTK4_CONFIG" ]]; then
    local target
    target="$(readlink -f "$GTK4_CONFIG" 2>/dev/null || true)"
    if [[ "$target" == "$OOZE_WS"/* ]]; then
      log "Removing Ooze-managed gtk-4.0 symlink (was overriding Ooze apps): $GTK4_CONFIG"
      rm -f "$GTK4_CONFIG"
    fi
  fi
}

backup_gtk4_if_needed() {
  if [[ ! -e "$GTK4_CONFIG" ]]; then
    return
  fi
  if [[ -L "$GTK4_CONFIG" ]]; then
    local target
    target="$(readlink -f "$GTK4_CONFIG" 2>/dev/null || true)"
    if [[ "$target" == "$OOZE_WS"/* ]]; then
      rm -f "$GTK4_CONFIG"
      return
    fi
    log "Removing non-Ooze gtk-4.0 symlink at $GTK4_CONFIG"
    rm -f "$GTK4_CONFIG"
    return
  fi
  if [[ -d "$GTK4_CONFIG" ]]; then
    local bak="${GTK4_CONFIG}.bak-ooze-$(date +%Y%m%d%H%M%S)"
    log "Backing up existing $GTK4_CONFIG -> $bak"
    mv "$GTK4_CONFIG" "$bak"
  fi
}

capture_libadwaita() {
  local color="$1"
  local dest="$2"

  backup_gtk4_if_needed
  rm -rf "$dest"
  mkdir -p "$(dirname "$dest")"

  (
    cd "$CACHE"
    ./install.sh -l -c "$color" -a normal
  )

  if [[ ! -d "$GTK4_CONFIG" ]]; then
    echo "error: WhiteSur -l did not create $GTK4_CONFIG" >&2
    exit 1
  fi

  mv "$GTK4_CONFIG" "$dest"
  log "Stashed libadwaita $color config at $dest (not linked into ~/.config)"
}

need_cmd git
need_cmd sassc
need_cmd glib-compile-resources

mkdir -p "$THEMES_DEST" "$OOZE_WS" "$ROOT/.cache"
clear_managed_gtk4_link

if [[ ! -d "$CACHE/.git" ]]; then
  log "Cloning WhiteSur into $CACHE"
  git clone --depth 1 "$REPO_URL" "$CACHE"
else
  log "Updating WhiteSur cache"
  git -C "$CACHE" pull --ff-only || true
fi

log "Installing WhiteSur-Light / WhiteSur-Dark into $THEMES_DEST"
(
  cd "$CACHE"
  ./install.sh -d "$THEMES_DEST" -c light -c dark -a normal -o normal
)

if [[ ! -d "$THEMES_DEST/WhiteSur-Light" ]]; then
  echo "error: expected $THEMES_DEST/WhiteSur-Light after install" >&2
  ls -la "$THEMES_DEST" | head -40 >&2 || true
  exit 1
fi

if [[ "$STASH_LIBADWAITA" == 1 ]]; then
  log "Stashing libadwaita GTK4 configs (NOT linking ~/.config/gtk-4.0)"
  capture_libadwaita light "$GTK4_LIGHT"
  capture_libadwaita dark "$GTK4_DARK"
fi

# Ensure we never leave a global gtk-4.0 override in place.
clear_managed_gtk4_link

date -Iseconds > "$MARKER"
echo "WhiteSur" >> "$MARKER"
echo "no-global-gtk4" >> "$MARKER"

log "Done."
log "  Themes: $THEMES_DEST/WhiteSur-{Light,Dark}"
log "  Foreign apps get GTK_THEME=WhiteSur-* when launched from Spot/Command."
log "  Ooze apps keep Adwaita + Ooze Gel (no ~/.config/gtk-4.0 override)."
