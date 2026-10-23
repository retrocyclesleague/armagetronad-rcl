#!/usr/bin/env bash
# Build an Armagetron Advanced **dedicated server** on Windows (MSYS2 MINGW64),
# in its own directory (build-server), from the dependencies the client build
# already has (_deps-win, built by build-windows-client.sh). Nothing is
# downloaded. It is the local test server for the client: run it with
# SERVER_IP 127.0.0.1 and TALK_TO_MASTER 0, and with --resourcedir pointing at
# build-client/resource/included so it fetches no maps.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-server"
DEPS="${ROOT}/_deps-win"
JOBS="${NUMBER_OF_PROCESSORS:-4}"
MINGW_PREFIX="${MINGW_PREFIX:-/mingw64}"

export PATH="${MINGW_PREFIX}/bin:${DEPS}/bin:${PATH}"
export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig:${MINGW_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export CPPFLAGS="-I${DEPS}/include ${CPPFLAGS:-}"
export LDFLAGS="-L${DEPS}/lib ${LDFLAGS:-}"

if ! test -f "${DEPS}/lib/libZThread.a" && ! test -f "${DEPS}/lib/libzthread.a"; then
  echo "ZThread is not in ${DEPS}: run build-windows-client.sh first" >&2
  exit 1
fi

cd "${ROOT}"
if ! test -r configure; then
  ./bootstrap.sh
fi

mkdir -p "${BUILD}"
# The login code is on (KRAWALL_SERVER), as servers have it; a tree configured
# without it is configured again.
if ! test -f "${BUILD}/Makefile" || grep -q "disable-armathentication" "${BUILD}/config.status" 2>/dev/null; then
  (
    cd "${BUILD}"
    ../configure \
      --enable-dedicated \
      --prefix="${BUILD}/install" \
      --with-zthread-prefix="${DEPS}" \
      --disable-restoreold \
      --enable-automakedefaults \
      --disable-useradd \
      --disable-sysinstall \
      --disable-initscripts \
      --disable-uninstall \
      --disable-etc \
      --disable-games \
      --disable-music
  )
fi

rm -f "${BUILD}/src/nTrueVersion.h"
# the Windows socket libraries, which the dedicated configure does not add
make -C "${BUILD}/src" -j"${JOBS}" armagetronad_main.exe LIBS="-lpthread -lxml2 -lws2_32 -lwsock32"
echo "Server binary: ${BUILD}/src/armagetronad_main.exe"
