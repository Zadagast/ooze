#!/usr/bin/env bash
# Rebuild Ooze and install it over the native /usr files.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

fail() {
  echo "dev-install: $*" >&2
  exit 1
}

[[ -f "$ROOT/meson.build" ]] || fail "run this script from an Ooze checkout"
command -v meson >/dev/null 2>&1 || fail "meson is not installed"
command -v ninja >/dev/null 2>&1 || fail "ninja is not installed"
command -v sudo >/dev/null 2>&1 || fail "sudo is not installed"
command -v pkg-config >/dev/null 2>&1 || fail "pkg-config is not installed"

if [[ "$(uname -s)" != Linux ]]; then
  fail "this script requires a Linux host with Mutter 18"
fi

if ! pkg-config --exists libmutter-18; then
  fail "libmutter-18 development files were not found; use an Ubuntu 26.04/Mutter 18 host"
fi

echo "==> Configuring Ooze with prefix=/usr"
if [[ -f "$BUILD_DIR/build.ninja" ]]; then
  meson configure "$BUILD_DIR" --prefix=/usr
else
  meson setup "$BUILD_DIR" --prefix=/usr
fi

echo "==> Building Ooze"
ninja -C "$BUILD_DIR"

echo "==> Installing Ooze under /usr"
sudo ninja -C "$BUILD_DIR" install

echo
echo "Install complete. Log out and back into the Ooze session for session-level changes to take effect."
