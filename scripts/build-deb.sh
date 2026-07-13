#!/usr/bin/env bash
# Build an amd64 .deb for Ooze (native GDM Wayland session + nested tester).
#
# Ships installed binaries + data under /usr. Host must provide Mutter 18
# and the Depends listed in packaging/deb/control.in.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
STAGE="${STAGE_DIR:-$DIST_DIR/ooze-deb-root}"
ARCH="${ARCH:-amd64}"
VERSION="${VERSION:-$(meson introspect --projectinfo "$BUILD_DIR" 2>/dev/null | sed -n 's/.*"version": "\([^"]*\)".*/\1/p' | head -1)}"
if [[ -z "$VERSION" ]]; then
  VERSION="$(sed -n "s/.*version: '\\([^']*\\)'.*/\\1/p" meson.build | head -1)"
fi
VERSION="${VERSION:-0.1.0}"
DEB_NAME="ooze_${VERSION}_${ARCH}.deb"
OUTPUT="${OUTPUT:-$DIST_DIR/$DEB_NAME}"

echo "==> Building Ooze ($VERSION)"
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  meson setup "$BUILD_DIR" --prefix=/usr
else
  meson configure "$BUILD_DIR" --prefix=/usr >/dev/null
fi
ninja -C "$BUILD_DIR"

if [[ ! -f "$ROOT/data/icons/elementary/index.theme" ]]; then
  echo "==> Fetching elementary icons"
  ninja -C "$BUILD_DIR" elementary-icons
fi

echo "==> Staging DESTDIR=$STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"
DESTDIR="$STAGE" ninja -C "$BUILD_DIR" install

# Nested launcher (Applications menu) + native GDM Wayland session
install -d "$STAGE/usr/bin"
install -m 0755 "$ROOT/packaging/deb/ooze-session" "$STAGE/usr/bin/ooze-session"
install -m 0755 "$ROOT/packaging/deb/ooze-wayland-session" \
  "$STAGE/usr/bin/ooze-wayland-session"
install -d "$STAGE/usr/share/applications"
install -m 0644 "$ROOT/packaging/deb/ooze-session.desktop" \
  "$STAGE/usr/share/applications/org.ooze.Desktop.desktop"
install -d "$STAGE/usr/share/wayland-sessions"
install -m 0644 "$ROOT/data/wayland-sessions/ooze.desktop" \
  "$STAGE/usr/share/wayland-sessions/ooze.desktop"

# Prefer a simple branded icon if present
if [[ -f "$ROOT/data/spot-logo.svg" ]]; then
  install -d "$STAGE/usr/share/icons/hicolor/scalable/apps"
  install -m 0644 "$ROOT/data/spot-logo.svg" \
    "$STAGE/usr/share/icons/hicolor/scalable/apps/ooze.svg"
fi

# Ship nest-friendly data under /usr/share/ooze when meson did not
if [[ -d "$ROOT/data" ]]; then
  install -d "$STAGE/usr/share/ooze"
  # Icons + desktop helpers used at runtime
  if [[ -d "$ROOT/data/icons/elementary" ]]; then
    rm -rf "$STAGE/usr/share/ooze/icons"
    mkdir -p "$STAGE/usr/share/ooze/icons"
    cp -a "$ROOT/data/icons/elementary" "$STAGE/usr/share/ooze/icons/"
  fi
fi

echo "==> Writing DEBIAN/control"
install -d "$STAGE/DEBIAN"
sed "s/@VERSION@/${VERSION}/g" "$ROOT/packaging/deb/control.in" > "$STAGE/DEBIAN/control"
# Installed-Size in KiB (exclude DEBIAN metadata from rough size)
INSTALLED_SIZE="$(du -sk --exclude=DEBIAN "$STAGE" | awk '{print $1}')"
printf 'Installed-Size: %s\n' "$INSTALLED_SIZE" >> "$STAGE/DEBIAN/control"

mkdir -p "$DIST_DIR"
echo "==> Building $OUTPUT"
rm -f "$OUTPUT"
dpkg-deb --root-owner-group --build "$STAGE" "$OUTPUT"
echo "OK: $OUTPUT"
ls -lh "$OUTPUT"
