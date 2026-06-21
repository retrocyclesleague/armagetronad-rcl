#!/usr/bin/env bash
# Build Armagetron Advanced **client** on Windows (MSYS2 MINGW64).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-client"
DEPS="${ROOT}/_deps-win"
JOBS="${NUMBER_OF_PROCESSORS:-4}"
MINGW_PREFIX="${MINGW_PREFIX:-/mingw64}"

export PATH="${MINGW_PREFIX}/bin:${DEPS}/bin:${PATH}"
export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig:${MINGW_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export CPPFLAGS="-I${DEPS}/include ${CPPFLAGS:-}"
export LDFLAGS="-L${DEPS}/lib ${LDFLAGS:-}"

build_zthread() {
  if test -f "${DEPS}/lib/libZThread.a" || test -f "${DEPS}/lib/libzthread.a"; then
    return 0
  fi

  echo "Building ZThread 2.3.2..."
  mkdir -p "${DEPS}"
  local work="/tmp/rcl-zthread-$$"
  mkdir -p "${work}"
  cd "${work}"

  curl -fsSL "https://sourceforge.net/projects/zthread/files/ZThread/2.3.2/ZThread-2.3.2.tar.gz/download" \
    -o ZThread-2.3.2.tar.gz
  rm -rf ZThread-2.3.2
  tar -xzf ZThread-2.3.2.tar.gz
  (
    cd ZThread-2.3.2
    libtoolize --copy --force
    ./configure --prefix="${DEPS}" --enable-shared=no
    make -j"${JOBS}"
    make install
  )
  rm -rf "${work}"
}

cd "${ROOT}"

if ! test -r configure; then
  ./bootstrap.sh
fi

build_zthread

mkdir -p "${BUILD}"
if ! test -f "${BUILD}/Makefile"; then
  (
    cd "${BUILD}"
    ../configure \
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
      --disable-armathentication \
      DEBUGLEVEL=3 \
      CODELEVEL=2
  )
fi

make -C "${BUILD}" -j"${JOBS}" all debug

BIN=""
for candidate in \
  "${BUILD}/src/armagetronad_main.exe" \
  "${BUILD}/src/armagetronad_main" \
  "${BUILD}/armagetronad_main.exe"; do
  if test -f "$candidate"; then
    BIN="$candidate"
    break
  fi
done

if test -z "$BIN"; then
  echo "error: client binary not found under ${BUILD}" >&2
  exit 1
fi

echo "Client binary: ${BIN}"
