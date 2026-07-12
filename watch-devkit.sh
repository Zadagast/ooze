#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if ! command -v entr >/dev/null 2>&1; then
  echo "watch-devkit requires entr."
  echo "Install it with: sudo apt install entr"
  exit 1
fi

echo "Watching src/ and spot/ — save a file to rebuild and restart devkit (Ctrl+C to stop)."
find "$ROOT/src" "$ROOT/spot" -type f \( -name '*.c' -o -name '*.h' \) | entr -r bash -c "
  set -euo pipefail
  cd '$ROOT'
  ninja -C build
  export GSETTINGS_BACKEND=keyfile
  export GSETTINGS_SCHEMA_DIR='${GSETTINGS_SCHEMA_DIR:-/usr/share/glib-2.0/schemas}'
  export MUTTER_DEBUG_DUMMY_MODE_SPECS=1920x1080
  export PATH='$ROOT/build:'\"$PATH\"
  export OOZE_DATA_DIR='$ROOT/data'
  export XDG_DATA_DIRS='$ROOT/data'\"\${XDG_DATA_DIRS:+:\$XDG_DATA_DIRS}\"
  gsettings set org.gnome.mutter edge-tiling true 2>/dev/null || true
  exec dbus-run-session '$ROOT/build/my-desktop' --wayland --devkit --no-x11
"
