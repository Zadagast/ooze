#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if ! command -v entr >/dev/null 2>&1; then
  echo "watch-devkit requires entr."
  echo "Install it with: sudo apt install entr"
  exit 1
fi

echo "Watching src/ — save a file to rebuild and restart devkit (Ctrl+C to stop)."
find "$ROOT/src" -type f \( -name '*.c' -o -name '*.h' \) | entr -r bash -c "
  set -euo pipefail
  cd '$ROOT'
  ninja -C build
  export GSETTINGS_BACKEND=memory
  export MUTTER_DEBUG_DUMMY_MODE_SPECS=1920x1080
  exec dbus-run-session '$ROOT/build/my-desktop' --wayland --devkit --no-x11
"
