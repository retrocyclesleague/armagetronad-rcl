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
test -x "$BINARY" || { echo "error: binary not executable: $BINARY" >&2; exit 1; }

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if test -z "$VERSION"; then
  VERSION="$(tr -d '\n' < "${ROOT}/major_version")"
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

PKG="Retrocycles-RCL-${VERSION}-${PLATFORM}"
mkdir -p "${STAGE}/${PKG}/bin"
cp "$BINARY" "${STAGE}/${PKG}/bin/armagetronad"
chmod +x "${STAGE}/${PKG}/bin/armagetronad"

cat > "${STAGE}/${PKG}/README.txt" <<EOF
Retrocycles RCL client beta — ${VERSION} (${PLATFORM})

Install (side-by-side with Steam Retrocycles):
  1. Extract this tarball anywhere (e.g. ~/Retrocycles-RCL)
  2. Run: ./bin/armagetronad
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
