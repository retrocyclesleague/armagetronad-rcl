#!/usr/bin/env bash
# Smoke: client binary exists and prints version-ish output (no display required).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

resolve_bin() {
  for candidate in \
    "${1:-}" \
    "$ROOT/build-client/src/armagetronad_main" \
    "$ROOT/src/armagetronad_main" \
    "$ROOT/build/src/armagetronad_main"; do
    if test -n "$candidate" && test -x "$candidate"; then
      echo "$candidate"
      return
    fi
  done
  echo "error: client binary not found — run build-linux-client.sh or build-macos.sh first" >&2
  exit 1
}

BIN="$(resolve_bin "${CLIENT_BIN:-}")"
MAJOR="$(tr -d '\n' < "$ROOT/major_version")"

echo "Smoke client: $BIN"
echo "Expected major_version: $MAJOR"

if strings "$BIN" | grep -q "$MAJOR"; then
  echo "Smoke OK — version string present in binary"
  exit 0
fi

echo "warning: $MAJOR not found via strings; checking binary executes..." >&2
set +e
if command -v timeout >/dev/null 2>&1; then
  timeout 3 "$BIN" --help > /tmp/rcl-client-smoke.log 2>&1
  status=$?
elif command -v gtimeout >/dev/null 2>&1; then
  gtimeout 3 "$BIN" --help > /tmp/rcl-client-smoke.log 2>&1
  status=$?
else
  "$BIN" --help > /tmp/rcl-client-smoke.log 2>&1 &
  pid=$!
  sleep 3
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null
  status=$?
fi
set -e
head -10 /tmp/rcl-client-smoke.log || true

if test "$status" -eq 0 || test "$status" -eq 124 || grep -qi "armagetron\|retrocycles\|usage" /tmp/rcl-client-smoke.log; then
  echo "Smoke OK (exit $status)"
  exit 0
fi

echo "Smoke FAILED (exit $status)" >&2
exit 1
