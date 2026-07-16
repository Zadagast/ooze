#!/usr/bin/env bash
# Install WhiteSur GTK theme for *foreign* apps only (via GTK_THEME on launch).
#
# Usage:
#   ./scripts/install-whitesur-theme.sh
#   DESTDIR=/tmp/package-root ./scripts/install-whitesur-theme.sh
#   OOZE_THEMES_DEST=/path/to/themes ./scripts/install-whitesur-theme.sh
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
# Pin upstream WhiteSur to a known-good commit so packaging is reproducible and
# a breaking change on their main branch cannot silently break our build.
REPO_REF="${WHITESUR_REF:-fd7d1a97cd6de09b53a0dae8f9749cdeb43d5a59}"

# WhiteSur's installer resolves the target user via
#   ${SUDO_USER:-$(logname ...)}
# On a headless/root CI runner there is no login session, so logname yields
# nothing and its passwd lookup aborts the installer under `set -e`. Pin the
# identity to the current user; since it equals the running user, the installer
# never attempts a sudo elevation.
export SUDO_USER="${SUDO_USER:-$(id -un)}"

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
if [[ -n "${OOZE_THEMES_DEST:-}" ]]; then
  THEMES_DEST="$OOZE_THEMES_DEST"
elif [[ -n "${DESTDIR:-}" ]]; then
  THEMES_DEST="${DESTDIR%/}/usr/share/ooze/themes"
else
  THEMES_DEST="${XDG_DATA_HOME}/themes"
fi
SYSTEM_INSTALL=0
if [[ "$THEMES_DEST" != "${XDG_DATA_HOME}/themes" ]]; then
  SYSTEM_INSTALL=1
fi
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

mkdir -p "$THEMES_DEST" "$ROOT/.cache"
if [[ "$SYSTEM_INSTALL" == 0 ]]; then
  mkdir -p "$OOZE_WS"
  clear_managed_gtk4_link
fi

if [[ ! -d "$CACHE/.git" ]]; then
  log "Cloning WhiteSur ($REPO_REF) into $CACHE"
  git init -q "$CACHE"
  git -C "$CACHE" remote add origin "$REPO_URL" 2>/dev/null || \
    git -C "$CACHE" remote set-url origin "$REPO_URL"
fi
if [[ "$(git -C "$CACHE" rev-parse --verify -q HEAD 2>/dev/null)" != "$REPO_REF" ]]; then
  log "Fetching pinned WhiteSur $REPO_REF"
  git -C "$CACHE" fetch --depth 1 origin "$REPO_REF"
  git -C "$CACHE" checkout -q FETCH_HEAD
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

if [[ "$STASH_LIBADWAITA" == 1 && "$SYSTEM_INSTALL" == 1 ]]; then
  echo "error: --stash-libadwaita is only available for a user installation" >&2
  exit 2
fi

if [[ "$STASH_LIBADWAITA" == 1 ]]; then
  log "Stashing libadwaita GTK4 configs (NOT linking ~/.config/gtk-4.0)"
  capture_libadwaita light "$GTK4_LIGHT"
  capture_libadwaita dark "$GTK4_DARK"
fi

# Ensure a user install never leaves a global gtk-4.0 override in place.
if [[ "$SYSTEM_INSTALL" == 0 ]]; then
  clear_managed_gtk4_link
fi

if [[ "$SYSTEM_INSTALL" == 0 ]]; then
  date -Iseconds > "$MARKER"
  echo "WhiteSur" >> "$MARKER"
  echo "no-global-gtk4" >> "$MARKER"
fi

log "Done."
log "  Themes: $THEMES_DEST/WhiteSur-{Light,Dark}"
log "  Foreign apps get scoped GTK_THEME=WhiteSur-* plus XSETTINGS."
log "  Ooze apps keep Adwaita + Ooze Gel (no ~/.config/gtk-4.0 override)."
