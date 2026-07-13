#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ ! -f build/ooze ]]; then
  echo "No build found; configuring and compiling..."
  meson setup build
  ninja -C build
fi

# Use the keyfile GSettings backend so the compositor and its child apps
# (Spot, Ooze Command) share the same settings store.  Writing
# "prefer-dark" / "prefer-light" to org.gnome.desktop.interface here will
# propagate to AdwStyleManager in every child process automatically.
# We point at a project-local file so dev runs don't touch the user's real
# dconf database.
export XDG_CURRENT_DESKTOP=Ooze
export XDG_SESSION_DESKTOP=Ooze
export DESKTOP_SESSION=Ooze
export GSETTINGS_BACKEND=keyfile
export GSETTINGS_SCHEMA_DIR="${GSETTINGS_SCHEMA_DIR:-/usr/share/glib-2.0/schemas}"
# Colon-separated modes the nest exposes via DisplayConfig (WWxHH or WWxHH@RR).
# Prefer 1600x900+ first: half work-area must fit large-min apps (Inkscape) for
# Mutter side-tile; 1280x720 often refuses can_tile_side_by_side.
# Override before launch, e.g. OOZE_DISPLAY_MODES=1280x720:1920x1080 ./run-devkit.sh
export MUTTER_DEBUG_DUMMY_MODE_SPECS="${OOZE_DISPLAY_MODES:-1600x900:1920x1080:2560x1440:1280x720}"
export PATH="$ROOT/build:$PATH"
# Optional link deps for ooze-torrent when built against third-party/sysdeps.
if [[ -d "$ROOT/third-party/libtransmission/sysdeps/lib" ]]; then
  export LD_LIBRARY_PATH="$ROOT/third-party/libtransmission/sysdeps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
export OOZE_DATA_DIR="$ROOT/data"
export XDG_DATA_DIRS="$ROOT/data${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export OOZE_THEMES_DIR="${OOZE_THEMES_DIR:-$ROOT/data/themes}"
# Isolated MIME defaults so Spot opens Ooze Eye for images in the nest
# without rewriting the host user's global mimeapps.list.
export XDG_CONFIG_HOME="${OOZE_XDG_CONFIG:-$ROOT/data/xdg-config}"
mkdir -p "$XDG_CONFIG_HOME"

# Portal UseIn=ooze. Do not set GTK_THEME session-wide (compositor/portals);
# foreign apps get WhiteSur only on their launch environ / XSETTINGS.
# shellcheck source=/dev/null
source "$ROOT/packaging/deb/ooze-session-env.sh"
ooze_export_foreign_gtk_theme
ooze_export_appmenu_modules

# Nest compositor serves Gtk/ShellShowsMenubar via built-in XSETTINGS when
# system xsettingsd is absent (see compositor/ooze-xsettings.c).

if ! [[ -f /usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libappmenu-gtk-module.so ]] && \
   ! [[ -f /usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libappmenu-gtk3-module.so ]]; then
  echo "NOTE: appmenu-gtk-module not installed. For Inkscape/GTK3 global menus run:"
  echo "  ./scripts/install-appmenu.sh"
fi

# Edge snap must be on before Mutter binds prefs (schema default is false).
gsettings set org.gnome.mutter edge-tiling true 2>/dev/null || true
gsettings set org.gnome.desktop.peripherals.mouse drag-threshold 20 2>/dev/null || true
# Super+Left / Super+Right tile (org.gnome.mutter.keybindings).
gsettings set org.gnome.mutter.keybindings toggle-tiled-left "['<Super>Left']" 2>/dev/null || true
gsettings set org.gnome.mutter.keybindings toggle-tiled-right "['<Super>Right']" 2>/dev/null || true

if [[ ! -f "$ROOT/data/icons/elementary/index.theme" ]]; then
  ninja -C build elementary-icons.stamp
fi

if [[ ! -f "$OOZE_THEMES_DIR/WhiteSur-Light/index.theme" ]] &&
   [[ ! -f "${XDG_DATA_HOME:-$HOME/.local/share}/themes/WhiteSur-Light/index.theme" ]] &&
   [[ ! -f "/usr/share/ooze/themes/WhiteSur-Light/index.theme" ]]; then
  echo "NOTE: WhiteSur theme not installed (Mac traffic lights for foreign GTK apps)."
  echo "  ./scripts/install-whitesur-theme.sh"
fi

# Xwayland stays enabled so appmenu-gtk-module (X11 registrar) can serve GTK3
# apps. Pure Wayland GtkApplication apps still use gtk_shell1 export.
# Pass OOZE_NO_X11=1 to force --no-x11.
desktop_args=(--wayland --devkit)
if [[ "${OOZE_NO_X11:-}" == "1" ]]; then
  desktop_args+=(--no-x11)
fi

# AppMenu registrar is D-Bus-activated (com.canonical.AppMenu.Registrar.service).
# Do not pre-spawn it — a second instance fails on org.valapanel.AppMenu.Registrar.
exec dbus-run-session -- "$ROOT/build/ooze" "${desktop_args[@]}"
