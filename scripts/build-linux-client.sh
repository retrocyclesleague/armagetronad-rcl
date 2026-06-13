#!/usr/bin/env bash
# Build Armagetron Advanced **client** on Linux (GL enabled — not dedicated server).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-client"
JOBS="$(nproc)"

cd "$ROOT"

if ! test -r configure; then
  ./bootstrap.sh
fi

mkdir -p "$BUILD"
if ! test -f "$BUILD/Makefile"; then
  (
    cd "$BUILD"
    ../configure \
      --prefix="${BUILD}/install" \
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

make -C "$BUILD" -j"$JOBS" all debug

BIN=""
for candidate in "$BUILD/src/armagetronad_main" "$BUILD/armagetronad_main"; do
  if test -x "$candidate"; then
    BIN="$candidate"
    break
  fi
done

if test -z "$BIN"; then
  echo "error: client binary not found under $BUILD" >&2
  exit 1
fi

echo "Client binary: $BIN"
