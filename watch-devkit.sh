#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if ! command -v entr >/dev/null 2>&1; then
  echo "watch-devkit requires entr."
  echo "Install it with: sudo apt install entr"
  exit 1
fi

echo "Watching compositor/ and spot/ — save a file to rebuild and restart devkit (Ctrl+C to stop)."
if [[ ! -f "$ROOT/data/themes/WhiteSur-Light/index.theme" ]] &&
   [[ ! -f "${XDG_DATA_HOME:-$HOME/.local/share}/themes/WhiteSur-Light/index.theme" ]] &&
   [[ ! -f "/usr/share/ooze/themes/WhiteSur-Light/index.theme" ]]; then
  echo "NOTE: WhiteSur theme not installed. For Mac traffic lights on foreign GTK apps:"
  echo "  ./scripts/install-whitesur-theme.sh"
fi
find "$ROOT/compositor" "$ROOT/spot" -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | entr -r bash -c "
  set -euo pipefail
  cd '$ROOT'
  ninja -C build
  export GSETTINGS_BACKEND=keyfile
  export GSETTINGS_SCHEMA_DIR='${GSETTINGS_SCHEMA_DIR:-/usr/share/glib-2.0/schemas}'
  export MUTTER_DEBUG_DUMMY_MODE_SPECS=\"\${OOZE_DISPLAY_MODES:-1600x900:1920x1080:2560x1440:1280x720}\"
  export PATH='$ROOT/build:'\"$PATH\"
  export OOZE_DATA_DIR='$ROOT/data'
  export XDG_DATA_DIRS='$ROOT/data'\"\${XDG_DATA_DIRS:+:\$XDG_DATA_DIRS}\"
  export OOZE_THEMES_DIR=\"\${OOZE_THEMES_DIR:-$ROOT/data/themes}\"
  export XDG_CONFIG_HOME=\"\${OOZE_XDG_CONFIG:-$ROOT/data/xdg-config}\"
  mkdir -p \"\$XDG_CONFIG_HOME\"
  # Unset GTK_THEME for compositor; foreign apps get WhiteSur on launch only.
  # shellcheck source=/dev/null
  source '$ROOT/packaging/deb/ooze-session-env.sh'
  ooze_export_foreign_gtk_theme
  ooze_export_appmenu_modules
  gsettings set org.gnome.mutter edge-tiling true 2>/dev/null || true
  gsettings set org.gnome.desktop.peripherals.mouse drag-threshold 20 2>/dev/null || true
  gsettings set org.gnome.mutter.keybindings toggle-tiled-left \"['<Super>Left']\" 2>/dev/null || true
  gsettings set org.gnome.mutter.keybindings toggle-tiled-right \"['<Super>Right']\" 2>/dev/null || true
  desktop_args=(--wayland --devkit)
  if [[ \"\${OOZE_NO_X11:-}\" == 1 ]]; then
    desktop_args+=(--no-x11)
  fi
  exec dbus-run-session '$ROOT/build/ooze' \"\${desktop_args[@]}\"
"
