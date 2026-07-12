#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ ! -f build/my-desktop ]]; then
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
export GSETTINGS_BACKEND=keyfile
export GSETTINGS_SCHEMA_DIR="${GSETTINGS_SCHEMA_DIR:-/usr/share/glib-2.0/schemas}"
export MUTTER_DEBUG_DUMMY_MODE_SPECS=1920x1080
export PATH="$ROOT/build:$PATH"
export OOZE_DATA_DIR="$ROOT/data"
export XDG_DATA_DIRS="$ROOT/data${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

if [[ ! -f "$ROOT/data/icons/elementary/index.theme" ]]; then
  ninja -C build elementary-icons
fi

exec dbus-run-session "$ROOT/build/my-desktop" --wayland --devkit --no-x11
