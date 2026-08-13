#!/usr/bin/env bash
# Build a relocatable Retrocycles RCL macOS .app from the native client binary.
set -euo pipefail

usage() {
  echo "Usage: $0 [--binary PATH] [--out-dir DIR] [--version VERSION] [--bundle-build NUMBER]" >&2
  exit 1
}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${ROOT}/src/armagetronad_main"
OUT_DIR="${ROOT}/dist"
VERSION=""
BUNDLE_BUILD=""

while test $# -gt 0; do
  case "$1" in
    --binary) BINARY="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --bundle-build) BUNDLE_BUILD="$2"; shift 2 ;;
    *) usage ;;
  esac
done

if test ! -x "$BINARY"; then
  echo "error: client binary is missing or not executable: $BINARY" >&2
  exit 1
fi
if test -z "$VERSION"; then
  VERSION="$(tr -d '\n' < "${ROOT}/major_version")"
fi
case "$VERSION" in
  ''|*[!0-9A-Za-z.+_-]*)
    echo "error: version contains characters unsafe for a bundle: $VERSION" >&2
    exit 1
    ;;
esac

BUNDLE_SHORT_VERSION="$(printf '%s\n' "$VERSION" | sed -nE 's/^([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p')"
if test -z "$BUNDLE_SHORT_VERSION"; then
  echo "error: version must begin with a numeric X.Y.Z version: $VERSION" >&2
  exit 1
fi
if test -z "$BUNDLE_BUILD"; then
  BUNDLE_BUILD="$(printf '%s\n' "$VERSION" | sed -nE 's/^.*[+]rcl\.([0-9]+)$/\1/p')"
fi
if test -z "$BUNDLE_BUILD"; then
  BUNDLE_BUILD="${GITHUB_RUN_NUMBER:-1}"
fi
case "$BUNDLE_BUILD" in
  ''|*[!0-9]*)
    echo "error: bundle build must contain digits only: $BUNDLE_BUILD" >&2
    exit 1
    ;;
esac

SOURCE_REPOSITORY="https://github.com/retrocyclesleague/armagetronad-rcl"
SOURCE_REVISION="${RCL_SOURCE_REF:-${GITHUB_SHA:-}}"
if test -z "$SOURCE_REVISION" && command -v git >/dev/null 2>&1; then
  if test -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal 2>/dev/null)"; then
    echo "error: refusing to label a dirty tree as exact corresponding source; commit it or set RCL_SOURCE_REF" >&2
    exit 1
  fi
  SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || true)"
fi
if test -z "$SOURCE_REVISION"; then
  echo "error: exact source revision unavailable; set RCL_SOURCE_REF to the published commit or tag" >&2
  exit 1
fi
case "$SOURCE_REVISION" in
  *[!0-9A-Za-z._/+:-]*)
    echo "error: source revision contains characters unsafe for Info.plist: $SOURCE_REVISION" >&2
    exit 1
    ;;
esac
SOURCE_URL="${SOURCE_REPOSITORY}/tree/${SOURCE_REVISION}"

SDL_IMAGE="${ROOT}/_deps/lib/libSDL_image-1.2.0.dylib"
SDL_COMPAT="/opt/homebrew/opt/sdl12-compat/lib/libSDL-1.2.0.dylib"
SDL2="/opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib"
LIBPNG="/opt/homebrew/opt/libpng/lib/libpng16.16.dylib"
ICON="${ROOT}/MacOS/Armagetron Advanced.icns"

for dependency in "$SDL_IMAGE" "$SDL_COMPAT" "$SDL2" "$LIBPNG" "$ICON"; do
  if test ! -f "$dependency"; then
    echo "error: required macOS bundle dependency not found: $dependency" >&2
    exit 1
  fi
done

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

APP="${STAGE}/Retrocycles RCL.app"
CONTENTS="${APP}/Contents"
MACOS="${CONTENTS}/MacOS"
FRAMEWORKS="${CONTENTS}/Frameworks"
RESOURCES="${CONTENTS}/Resources"
DOCUMENTATION="${RESOURCES}/Documentation"
FINAL_APP="${OUT_DIR}/Retrocycles RCL.app"
mkdir -p "$MACOS" "$FRAMEWORKS" "$RESOURCES" "$DOCUMENTATION"

cp "$BINARY" "${MACOS}/armagetronad"
chmod +x "${MACOS}/armagetronad"
cp "$SDL_IMAGE" "${FRAMEWORKS}/libSDL_image-1.2.0.dylib"
cp "$SDL_COMPAT" "${FRAMEWORKS}/libSDL-1.2.0.dylib"
cp "$SDL2" "${FRAMEWORKS}/libSDL2-2.0.0.dylib"
cp "$LIBPNG" "${FRAMEWORKS}/libpng16.16.dylib"
cp "$ICON" "${RESOURCES}/Retrocycles RCL.icns"

for document in COPYING.txt README-RCL.md THIRD_PARTY_NOTICES.md; do
  if test ! -f "${ROOT}/${document}"; then
    echo "error: distribution document not found: ${ROOT}/${document}" >&2
    exit 1
  fi
  cp "${ROOT}/${document}" "${DOCUMENTATION}/${document}"
done

cat > "${DOCUMENTATION}/SOURCE_INFO.txt" <<EOF
Retrocycles RCL build ID: ${VERSION}
Source repository: ${SOURCE_REPOSITORY}
Exact source revision: ${SOURCE_REVISION}
Corresponding source: ${SOURCE_URL}
EOF

for data_dir in config language models sound textures; do
  cp -R "${ROOT}/${data_dir}" "${RESOURCES}/${data_dir}"
done
mkdir -p "${RESOURCES}/resource"
cp -R "${ROOT}/resource/included" "${RESOURCES}/resource/included"

for required_file in \
  config/default.cfg \
  language/languages.txt \
  models/cycle_body.mod \
  resource/included/map.dtd \
  sound/cyclrun.wav \
  textures/font_rcl.png \
  textures/floor.png \
  textures/title.png; do
  if test ! -f "${RESOURCES}/${required_file}"; then
    echo "error: required app resource not packaged: ${required_file}" >&2
    exit 1
  fi
done

cat > "${MACOS}/retrocycles-rcl" <<'EOF'
#!/bin/sh
set -eu
RCL_CONTENTS=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RCL_PROFILE="${HOME}/Library/Application Support/Retrocycles RCL Client"
mkdir -p "$RCL_PROFILE"

exec "${RCL_CONTENTS}/MacOS/armagetronad" \
  --datadir "${RCL_CONTENTS}/Resources" \
  --configdir "${RCL_CONTENTS}/Resources/config" \
  --userdatadir "$RCL_PROFILE" \
  "$@"
EOF
chmod +x "${MACOS}/retrocycles-rcl"

cat > "${CONTENTS}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>Retrocycles RCL</string>
  <key>CFBundleExecutable</key><string>retrocycles-rcl</string>
  <key>CFBundleIconFile</key><string>Retrocycles RCL.icns</string>
  <key>CFBundleIdentifier</key><string>com.retrocyclesleague.client</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>Retrocycles RCL</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${BUNDLE_SHORT_VERSION}</string>
  <key>CFBundleVersion</key><string>${BUNDLE_BUILD}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>RCLBuildID</key><string>${VERSION}</string>
  <key>RCLSourceRevision</key><string>${SOURCE_REVISION}</string>
  <key>RCLSourceURL</key><string>${SOURCE_URL}</string>
</dict>
</plist>
EOF

install_name_tool \
  -change "$SDL_IMAGE" '@executable_path/../Frameworks/libSDL_image-1.2.0.dylib' \
  -change "$SDL_COMPAT" '@executable_path/../Frameworks/libSDL-1.2.0.dylib' \
  -change "$LIBPNG" '@executable_path/../Frameworks/libpng16.16.dylib' \
  "${MACOS}/armagetronad"

install_name_tool \
  -id '@rpath/libSDL_image-1.2.0.dylib' \
  -change "$SDL_COMPAT" '@loader_path/libSDL-1.2.0.dylib' \
  -change "$LIBPNG" '@loader_path/libpng16.16.dylib' \
  "${FRAMEWORKS}/libSDL_image-1.2.0.dylib"
install_name_tool -id '@rpath/libSDL-1.2.0.dylib' \
  "${FRAMEWORKS}/libSDL-1.2.0.dylib"
install_name_tool -id '@rpath/libSDL2-2.0.0.dylib' \
  "${FRAMEWORKS}/libSDL2-2.0.0.dylib"
install_name_tool -id '@rpath/libpng16.16.dylib' \
  "${FRAMEWORKS}/libpng16.16.dylib"

if otool -L "${MACOS}/armagetronad" "${FRAMEWORKS}"/*.dylib | \
    grep -E '/Users/|/opt/homebrew/' >/dev/null; then
  echo "error: bundle still contains a machine-local dynamic library path" >&2
  otool -L "${MACOS}/armagetronad" "${FRAMEWORKS}"/*.dylib >&2
  exit 1
fi

codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
plutil -lint "${CONTENTS}/Info.plist" >/dev/null

mkdir -p "$OUT_DIR"
if test -e "$FINAL_APP"; then
  echo "error: output app already exists; move or remove it first: $FINAL_APP" >&2
  exit 1
fi
cp -R "$APP" "$FINAL_APP"

ARCHIVE="${OUT_DIR}/Retrocycles-RCL-${VERSION}-macos-arm64.zip"
if test -e "$ARCHIVE" || test -e "${ARCHIVE}.sha256"; then
  echo "error: output archive already exists; move or remove it first: $ARCHIVE" >&2
  exit 1
fi
ditto -c -k --sequesterRsrc --keepParent "$FINAL_APP" "$ARCHIVE"
shasum -a 256 "$ARCHIVE" > "${ARCHIVE}.sha256"

echo "App:     $FINAL_APP"
echo "Archive: $ARCHIVE"
