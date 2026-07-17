#!/usr/bin/env bash
# Build a portable Ooze AppImage (nested --devkit session).
#
# Ships Ooze binaries + data. Relies on the host for Mutter 18 and typical
# GTK4/Adwaita/VTE libraries (same class of machine you develop on).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ARCH="${ARCH:-$(uname -m)}"
case "$ARCH" in
  x86_64|amd64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=aarch64 ;;
esac

BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
APPDIR="${APPDIR_PATH:-$DIST_DIR/Ooze.AppDir}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/packaging/appimage/tools}"
VERSION="${VERSION:-$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo 0.2.0)}"
OUTPUT="${OUTPUT:-$DIST_DIR/Ooze-${VERSION}-${ARCH}.AppImage}"

# Required nested-session binaries (my-desktop was renamed to ooze).
BINARIES=(
  ooze
  spot
  ooze-command
  ooze-king
  ooze-launch
  ooze-ear
  ooze-pak
  ooze-themes
  ooze-eye
  ooze-monitor
  ooze-about
)
# Optional: only shipped when meson enables the target.
OPTIONAL_BINARIES=(ooze-torrent)

echo "==> Building Ooze"
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  meson setup "$BUILD_DIR"
fi
ninja -C "$BUILD_DIR"

if [[ ! -f "$ROOT/data/icons/elementary/index.theme" ]]; then
  echo "==> Fetching elementary icons"
  ninja -C "$BUILD_DIR" elementary-icons.stamp
fi

for bin in "${BINARIES[@]}"; do
  if [[ ! -x "$BUILD_DIR/$bin" ]]; then
    echo "error: missing build/$bin (is the target enabled?)" >&2
    exit 1
  fi
done

echo "==> Assembling AppDir at $APPDIR"
rm -rf "$APPDIR"
mkdir -p \
  "$APPDIR/usr/bin" \
  "$APPDIR/usr/lib" \
  "$APPDIR/usr/share/ooze" \
  "$APPDIR/usr/share/applications" \
  "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
  "$APPDIR/usr/share/metainfo"

install_bins=()
for bin in "${BINARIES[@]}"; do
  install_bins+=("$BUILD_DIR/$bin")
done
install -m755 "${install_bins[@]}" "$APPDIR/usr/bin/"
for bin in "${OPTIONAL_BINARIES[@]}"; do
  if [[ -x "$BUILD_DIR/$bin" ]]; then
    install -m755 "$BUILD_DIR/$bin" "$APPDIR/usr/bin/"
  fi
done

# OozeKit is a project-owned shared library. Bundle its complete soname set
# alongside the AppImage binaries; host GTK/Mutter libraries remain external.
cp -a "$BUILD_DIR"/libooze-kit.so* "$APPDIR/usr/lib/"

# Runtime data (icons, logos, desktop files consumed via OOZE_DATA_DIR).
cp -a "$ROOT/data/." "$APPDIR/usr/share/ooze/"

# Host-facing desktop entry + icon for the AppImage itself.
install -m644 "$ROOT/packaging/appimage/ooze.desktop" "$APPDIR/ooze.desktop"
install -m644 "$ROOT/packaging/appimage/ooze.desktop" \
  "$APPDIR/usr/share/applications/ooze.desktop"

# Prefer SVG as AppImage icon; also place under hicolor.
ICON_SRC="$ROOT/data/spot-logo.svg"
install -m644 "$ICON_SRC" "$APPDIR/ooze.svg"
install -m644 "$ICON_SRC" \
  "$APPDIR/usr/share/icons/hicolor/scalable/apps/ooze.svg"
# Some AppImage helpers still look for Icon=ooze.png next to the desktop file.
if command -v rsvg-convert >/dev/null 2>&1; then
  rsvg-convert -w 256 -h 256 "$ICON_SRC" -o "$APPDIR/ooze.png"
elif command -v convert >/dev/null 2>&1; then
  convert -background none -resize 256x256 "$ICON_SRC" "$APPDIR/ooze.png"
else
  # Fallback: symlink svg so Icon=ooze still resolves somehow.
  ln -sf ooze.svg "$APPDIR/ooze.png"
fi

install -m755 "$ROOT/packaging/appimage/AppRun" "$APPDIR/AppRun"

# Optional AppStream metadata (harmless if ignored).
cat > "$APPDIR/usr/share/metainfo/org.ooze.Desktop.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>org.ooze.Desktop</id>
  <name>Ooze</name>
  <summary>Aqua-inspired Wayland desktop on Mutter</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-2.0-or-later</project_license>
  <developer id="org.ooze">
    <name>Ooze</name>
  </developer>
  <url type="homepage">https://github.com/ooze-desktop/ooze</url>
  <description>
    <p>
      Nested Ooze desktop session for trying the shell and first-party apps.
      Requires host Mutter 18 (libmutter-18) and GTK4/Adwaita/VTE.
    </p>
  </description>
  <launchable type="desktop-id">ooze.desktop</launchable>
  <content_rating type="oars-1.1"/>
</component>
EOF

mkdir -p "$DIST_DIR" "$TOOLS_DIR"

APPIMAGETOOL="$TOOLS_DIR/appimagetool-${ARCH}.AppImage"
if [[ ! -x "$APPIMAGETOOL" ]]; then
  echo "==> Downloading appimagetool"
  url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage"
  curl -fsSL -o "$APPIMAGETOOL" "$url"
  chmod +x "$APPIMAGETOOL"
fi

echo "==> Creating $OUTPUT"
# appimagetool may need FUSE; --appimage-extract-and-run avoids that.
export ARCH
if "$APPIMAGETOOL" --appimage-extract-and-run "$APPDIR" "$OUTPUT" 2>/dev/null; then
  :
elif "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"; then
  :
else
  echo "error: appimagetool failed" >&2
  exit 1
fi

chmod +x "$OUTPUT"
echo
echo "Built: $OUTPUT"
echo "Run:   $OUTPUT"
echo
echo "Note: needs host Mutter 18 (libmutter-18) and a Wayland session."
