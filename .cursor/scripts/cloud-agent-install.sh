#!/usr/bin/env bash
# Runs after Cursor checks out armagetronad-rcl. Idempotent.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ "${CLOUD_AGENT_SKIP_BUILD:-}" == "true" ]]; then
  echo "CLOUD_AGENT_SKIP_BUILD=true — skipping configure/make"
  exit 0
fi

if ! test -r configure; then
  ./bootstrap.sh
fi

mkdir -p build
if ! test -f build/Makefile; then
  (
    cd build
    ../configure --prefix=/app/server --disable-glout DEBUGLEVEL=3 CODELEVEL=2
  )
fi

make -C build -j"$(nproc)" all debug

if test -x build/armagetronad-dedicated; then
  echo "Dedicated binary: $(readlink -f build/armagetronad-dedicated)"
elif test -x build/src/armagetronad_main; then
  ln -sf src/armagetronad_main build/armagetronad-dedicated
  echo "Dedicated binary: build/armagetronad-dedicated (symlink to armagetronad_main)"
else
  echo "error: dedicated binary not found after build" >&2
  exit 1
fi
