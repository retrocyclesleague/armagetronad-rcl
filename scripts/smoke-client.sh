#!/usr/bin/env bash
# Smoke: the client binary exists and contains the expected build identity.
# This intentionally never launches the graphical client.
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
EXPECTED="${EXPECTED_VERSION:-$(tr -d '\n' < "$ROOT/major_version")}"

echo "Smoke client: $BIN"
echo "Expected version: $EXPECTED"

if strings "$BIN" | grep -F -- "$EXPECTED" >/dev/null; then
  echo "Smoke OK — version string present in binary"
  exit 0
fi

echo "Smoke FAILED — expected version not found in binary: $EXPECTED" >&2
exit 1
