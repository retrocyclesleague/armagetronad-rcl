#!/usr/bin/env bash
# Package a built client binary into Retrocycles-RCL-{version}-{platform}.tar.gz
set -euo pipefail

usage() {
  echo "Usage: $0 --binary PATH --platform PLATFORM [--version VERSION] [--out-dir DIR]" >&2
  exit 1
}

BINARY=""
PLATFORM=""
VERSION=""
OUT_DIR="${PWD}"

while test $# -gt 0; do
  case "$1" in
    --binary) BINARY="$2"; shift 2 ;;
    --platform) PLATFORM="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    *) usage ;;
  esac
done

test -n "$BINARY" && test -n "$PLATFORM" || usage
if test ! -f "$BINARY"; then
  echo "error: binary not found: $BINARY" >&2
  exit 1
fi
if test -x "$BINARY"; then
  :
elif test "${BINARY##*.}" != exe; then
  echo "error: binary not executable: $BINARY" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if test -z "$VERSION"; then
  VERSION="$(tr -d '\n' < "${ROOT}/major_version")"
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

PKG="Retrocycles-RCL-${VERSION}-${PLATFORM}"
mkdir -p "${STAGE}/${PKG}/bin"

EXE_NAME="armagetronad"
if test "${BINARY##*.}" = exe || test "${PLATFORM}" = windows-x86_64; then
  EXE_NAME="armagetronad.exe"
fi

cp "$BINARY" "${STAGE}/${PKG}/bin/${EXE_NAME}"
if test "${EXE_NAME}" != "armagetronad.exe"; then
  chmod +x "${STAGE}/${PKG}/bin/${EXE_NAME}"
fi

if test "${PLATFORM}" = windows-x86_64; then
  RUN_LINE="  2. Run: bin\\armagetronad.exe (or double-click after extracting)"
  EXAMPLE_DIR="C:\\Retrocycles-RCL"
else
  RUN_LINE="  2. Run: ./bin/armagetronad"
  EXAMPLE_DIR="~/Retrocycles-RCL"
fi

cat > "${STAGE}/${PKG}/README.txt" <<EOF
Retrocycles RCL client beta — ${VERSION} (${PLATFORM})

Install (side-by-side with Steam Retrocycles):
  1. Extract this archive anywhere (e.g. ${EXAMPLE_DIR})
${RUN_LINE}
  3. Verify About shows ${VERSION}

Smoke checklist + issue tracker:
  https://linear.app/retrocyclesleague/project/rcl-client-beta-fcc879adf357

Every bug report must include build ID: ${VERSION}
Steam Retrocycles remains the public default; this build is opt-in team beta.
EOF

mkdir -p "$OUT_DIR"
ARCHIVE="${OUT_DIR}/${PKG}.tar.gz"
tar -C "$STAGE" -czf "$ARCHIVE" "$PKG"
sha256sum "$ARCHIVE" | tee "${ARCHIVE}.sha256"
echo "Packaged: $ARCHIVE"
