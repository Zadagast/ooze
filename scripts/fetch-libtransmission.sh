#!/usr/bin/env bash
# Fetch a project-local static libtransmission for Ooze Torrent.
#
# Idempotent: if third-party/libtransmission/lib/libtransmission.a already
# exists (with companion static libs from a source build), exits successfully.
#
# Builds a pinned Transmission release with clients/daemon/utils/tests OFF and
# installs libtransmission.a plus bundled dht/utp/wildmat/crcany into
# third-party/libtransmission/.
#
# Optional: OOZE_TRANSMISSION_VERSION (default 4.1.1)
# Optional: portable cmake under .cache/cmake is used when system cmake is absent.
#
# Usage:
#   ./scripts/fetch-libtransmission.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${OOZE_TRANSMISSION_VERSION:-4.1.1}"
PREFIX="$ROOT/third-party/libtransmission"
SRC_PARENT="$ROOT/third-party/transmission-src"
CACHE="$ROOT/.cache/transmission"
ARCHIVE="$CACHE/transmission-${VERSION}.tar.xz"
URL="https://github.com/transmission/transmission/releases/download/${VERSION}/transmission-${VERSION}.tar.xz"

log() { echo "fetch-libtransmission: $*"; }

have_cmake() {
  if command -v cmake >/dev/null 2>&1; then
    return 0
  fi
  local portable
  portable="$(echo "$ROOT"/.cache/cmake/cmake-*-linux-x86_64/bin/cmake)"
  if [[ -x $portable ]]; then
    PATH="$(dirname "$portable"):$PATH"
    export PATH
    return 0
  fi
  return 1
}

install_ok() {
  [[ -f "$PREFIX/lib/libtransmission.a" \
    && -f "$PREFIX/lib/libdht.a" \
    && -f "$PREFIX/lib/libutp.a" \
    && -f "$PREFIX/lib/libwildmat.a" \
    && -f "$PREFIX/lib/libMadlerCrcany.a" \
    && -f "$PREFIX/include/transmission/transmission.h" \
    && -f "$PREFIX/include/transmission/variant.h" ]]
}

ensure_include_symlink() {
  mkdir -p "$PREFIX/include"
  ln -sfn transmission "$PREFIX/include/libtransmission"
}

if install_ok; then
  ensure_include_symlink
  log "already present at $PREFIX"
  exit 0
fi

mkdir -p "$CACHE" "$PREFIX/include/transmission" "$PREFIX/lib"

build_from_source() {
  local src build gen=()
  src="$SRC_PARENT/transmission-${VERSION}"
  build="$SRC_PARENT/build-${VERSION}"

  have_cmake || return 1
  command -v ninja >/dev/null 2>&1 || command -v make >/dev/null 2>&1 || return 1

  mkdir -p "$SRC_PARENT"

  if [[ ! -f "$ARCHIVE" ]]; then
    log "downloading $URL"
    curl -fL --retry 3 -o "$ARCHIVE.partial" "$URL"
    mv "$ARCHIVE.partial" "$ARCHIVE"
  fi

  if [[ ! -f "$src/CMakeLists.txt" ]]; then
    log "extracting $ARCHIVE"
    rm -rf "$src"
    tar -C "$SRC_PARENT" -xJf "$ARCHIVE"
    if [[ ! -f "$src/CMakeLists.txt" ]]; then
      local top
      top="$(find "$SRC_PARENT" -mindepth 1 -maxdepth 1 -type d | head -1)"
      [[ -n "$top" && -f "$top/CMakeLists.txt" ]] || return 1
      src="$top"
    fi
  fi

  if command -v ninja >/dev/null 2>&1; then
    gen=(-G Ninja)
  fi

  log "configuring cmake build in $build"
  rm -rf "$build"
  cmake -S "$src" -B "$build" "${gen[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DENABLE_GTK=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_MAC=OFF \
    -DENABLE_DAEMON=OFF \
    -DENABLE_UTILS=OFF \
    -DENABLE_CLI=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_NLS=OFF \
    -DINSTALL_DOC=OFF \
    -DINSTALL_WEB=OFF \
    -DREBUILD_WEB=OFF \
    -DENABLE_UTP=ON

  log "building libtransmission"
  cmake --build "$build" --target transmission

  local lib_a
  lib_a="$(find "$build" -path '*/libtransmission/libtransmission.a' -print -quit || true)"
  [[ -n "$lib_a" && -f "$lib_a" ]] || return 1
  cp -a "$lib_a" "$PREFIX/lib/libtransmission.a"

  # Bundled third-party static libs required when linking libtransmission.a
  local dht utp wildmat crc
  dht="$(find "$build/third-party" -name 'libdht.a' -print -quit || true)"
  utp="$(find "$build/third-party" -name 'libutp.a' -print -quit || true)"
  wildmat="$(find "$build/third-party" -name 'libwildmat.a' -print -quit || true)"
  crc="$(find "$build/third-party" -name 'libMadlerCrcany.a' -print -quit || true)"
  [[ -n "$dht" && -n "$utp" && -n "$wildmat" && -n "$crc" ]] || return 1
  cp -a "$dht" "$PREFIX/lib/libdht.a"
  cp -a "$utp" "$PREFIX/lib/libutp.a"
  cp -a "$wildmat" "$PREFIX/lib/libwildmat.a"
  cp -a "$crc" "$PREFIX/lib/libMadlerCrcany.a"

  mkdir -p "$PREFIX/include/transmission"
  cp -a "$src/libtransmission/transmission.h" "$PREFIX/include/transmission/"
  cp -a "$src/libtransmission/variant.h" "$PREFIX/include/transmission/"
  cp -a "$src/libtransmission/error.h" "$PREFIX/include/transmission/"
  cp -a "$src/libtransmission/error-types.h" "$PREFIX/include/transmission/" 2>/dev/null || true
  cp -a "$src/libtransmission/quark.h" "$PREFIX/include/transmission/"
  cp -a "$src/libtransmission/tr-macros.h" "$PREFIX/include/transmission/"
  cp -a "$src/libtransmission/values.h" "$PREFIX/include/transmission/" 2>/dev/null || true
  cp -a "$src/libtransmission/utils.h" "$PREFIX/include/transmission/" 2>/dev/null || true
  if [[ -f "$build/libtransmission/version.h" ]]; then
    cp -a "$build/libtransmission/version.h" "$PREFIX/include/transmission/"
  fi

  ensure_include_symlink
  install_ok
}

if build_from_source; then
  log "installed from source to $PREFIX"
else
  echo "error: could not build libtransmission from source." >&2
  echo "  Need cmake (system or .cache/cmake portable), ninja/make, curl, and" >&2
  echo "  development packages: libcurl, libevent, openssl, libdeflate, miniupnpc, natpmp." >&2
  exit 1
fi

ls -lah "$PREFIX/lib/"*.a "$PREFIX/include/transmission/transmission.h"
