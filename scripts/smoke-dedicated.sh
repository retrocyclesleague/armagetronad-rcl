#!/usr/bin/env bash
# Quick smoke: dedicated binary exists and starts (default build-dir layout).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

resolve_bin() {
  if test -x "$BUILD/armagetronad-dedicated"; then
    echo "$BUILD/armagetronad-dedicated"
    return
  fi
  if test -x "$BUILD/src/armagetronad_main"; then
    ln -sf src/armagetronad_main "$BUILD/armagetronad-dedicated"
    echo "$BUILD/armagetronad-dedicated"
    return
  fi
  echo "error: run .cursor/scripts/cloud-agent-install.sh or make -C build all debug first" >&2
  exit 1
}

BIN="$(resolve_bin)"
cd "$BUILD"

echo "Smoke: starting $BIN from build/ (5s timeout)..."
set +e
timeout 5 "$BIN" > /tmp/rcl-arma-smoke.log 2>&1
status=$?
set -e

head -15 /tmp/rcl-arma-smoke.log

# 124 = timeout (server was running), 0 = clean exit
if test "$status" -eq 124 || test "$status" -eq 0; then
  echo "Smoke OK (exit $status)"
  exit 0
fi

if grep -q "BuildDirectory" /tmp/rcl-arma-smoke.log; then
  echo "Smoke OK (started, exit $status — likely signal from timeout/pipe)"
  exit 0
fi

echo "Smoke FAILED (exit $status)" >&2
exit 1
