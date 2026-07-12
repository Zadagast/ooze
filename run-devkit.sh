#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ ! -f build/my-desktop ]]; then
  echo "No build found; configuring and compiling..."
  meson setup build
  ninja -C build
fi

export GSETTINGS_BACKEND=memory
export MUTTER_DEBUG_DUMMY_MODE_SPECS=1920x1080

exec dbus-run-session "$ROOT/build/my-desktop" --wayland --devkit --no-x11
