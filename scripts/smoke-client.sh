#!/usr/bin/env bash
# Smoke: verify build identity, then optionally run the packaged launcher's
# non-graphical install self-check against an isolated temporary profile.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

resolve_bin() {
  for candidate in \
    "${1:-}" \
    "$ROOT/build-client/src/armagetronad_main.exe" \
    "$ROOT/build-client/src/armagetronad_main" \
    "$ROOT/src/armagetronad_main.exe" \
    "$ROOT/src/armagetronad_main" \
    "$ROOT/build/src/armagetronad_main.exe" \
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
EXPECTED="${EXPECTED_VERSION:-$(tr -d '\r\n' < "$ROOT/major_version")}"

echo "Smoke client: $BIN"
echo "Expected version: $EXPECTED"

if ! strings "$BIN" | grep -F -- "$EXPECTED" >/dev/null; then
  echo "Smoke FAILED — expected version not found in binary: $EXPECTED" >&2
  exit 1
fi

echo "Version identity OK"

if test -n "${PACKAGE_ROOT:-}"; then
  LAUNCHER="${PACKAGE_ROOT}/Retrocycles-RCL.exe"
  PACKAGED_BIN="${PACKAGE_ROOT}/bin/armagetronad.exe"
  test -f "$LAUNCHER" || {
    echo "Smoke FAILED — packaged launcher not found: $LAUNCHER" >&2
    exit 1
  }
  test -f "$PACKAGED_BIN" || {
    echo "Smoke FAILED — packaged client not found: $PACKAGED_BIN" >&2
    exit 1
  }

  PROFILE="$(mktemp -d)"
  trap 'rm -rf "$PROFILE"' EXIT
  echo "Smoke package: $PACKAGE_ROOT"
  "$LAUNCHER" --rcl-check-install --userdatadir "$PROFILE"
  echo "Packaged launcher and install layout OK"
fi

echo "Smoke OK"
