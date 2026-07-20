#!/usr/bin/env bash
# Bump the Ooze version everywhere in one command.
#
# Usage:
#   ./scripts/bump-version.sh 0.5.0
#
# meson.build is the source of truth; build-deb.sh, ooze-update, and
# build-appimage.sh all read it at build time. The remaining places a
# human-visible version string appears (README, site downloads, the AppImage
# desktop entry) are rewritten here so a release is a single command + PR.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEW="${1:-}"

if [[ ! "$NEW" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Usage: $0 X.Y.Z" >&2
  exit 1
fi

OLD="$(sed -n "s/.*version: '\([^']*\)'.*/\1/p" "$ROOT/meson.build" | head -1)"
if [[ -z "$OLD" ]]; then
  echo "ERROR: could not read current version from meson.build" >&2
  exit 1
fi
if [[ "$OLD" == "$NEW" ]]; then
  echo "Version is already $NEW" >&2
  exit 1
fi

echo "==> $OLD -> $NEW"
sed -i "s/version: '$OLD'/version: '$NEW'/" "$ROOT/meson.build"
sed -i "s/^X-AppImage-Version=.*/X-AppImage-Version=$NEW/" \
  "$ROOT/packaging/appimage/ooze.desktop"
OLD_RE="${OLD//./\\.}"
sed -i "s/$OLD_RE/$NEW/g" "$ROOT/README.md" "$ROOT/site/index.html"

echo "==> Updated:"
grep -rln "$NEW" "$ROOT/meson.build" "$ROOT/packaging/appimage/ooze.desktop" \
  "$ROOT/README.md" "$ROOT/site/index.html"
echo "==> Review with: git diff"
