#!/usr/bin/env bash
# Build a relocatable Retrocycles RCL macOS .app from the native client binary.
set -euo pipefail

usage() {
  echo "Usage: $0 [--binary PATH] [--out-dir DIR] [--version VERSION] [--bundle-build NUMBER] [--adhoc | --sign | --notarize]" >&2
  exit 1
}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${ROOT}/src/armagetronad_main"
OUT_DIR="${ROOT}/dist"
VERSION=""
BUNDLE_BUILD=""
ADHOC=true
NOTARIZE=false

while test $# -gt 0; do
  case "$1" in
    --adhoc) ADHOC=true; shift ;;
    --sign) ADHOC=false; shift ;;
    --notarize) NOTARIZE=true; ADHOC=false; shift ;;
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
  VERSION="$(sh "${ROOT}/batch/make/version" "$ROOT")"
fi
case "$VERSION" in
  ''|*[!0-9A-Za-z.+_-]*)
    echo "error: version contains characters unsafe for a bundle: $VERSION" >&2
    exit 1
    ;;
esac

if ! strings "$BINARY" | grep -F -- "$VERSION" >/dev/null; then
  echo "Build ID mismatch: binary does not contain $VERSION. Rebuild before packaging." >&2; exit 1
fi

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
SOURCE_REVISION="$(git -C "$ROOT" rev-parse HEAD)"
DIRTY="$(git -C "$ROOT" status --porcelain --untracked-files=normal)"
if ! $ADHOC && test -n "$DIRTY"; then
  echo 'Developer ID packaging requires committed source.' >&2; exit 1
fi
ARCH="$(lipo -archs "$BINARY")"
case "$ARCH" in arm64|x86_64) ;; *) echo 'Build a single architecture; universal packaging is not supported yet.' >&2; exit 1;; esac
FINAL_APP="${OUT_DIR}/Retrocycles RCL.app"
SUFFIX=""
if $ADHOC; then SUFFIX=-adhoc; fi
ARCHIVE="${OUT_DIR}/Retrocycles-RCL-${VERSION}-macos-${ARCH}${SUFFIX}.zip"
for output in "$FINAL_APP" "$ARCHIVE" "${ARCHIVE}.sha256" "$OUT_DIR/notary-result.json"; do
  if test -e "$output"; then
    echo "error: output already exists; choose a fresh output directory: $output" >&2
    exit 1
  fi
done
if $NOTARIZE; then
  ! $ADHOC || { echo 'Ad-hoc builds cannot be notarized.' >&2; exit 1; }
  : "${RCL_NOTARY_PROFILE:?Set a notarytool keychain profile}"
fi
SOURCE_URL="${SOURCE_REPOSITORY}/tree/${SOURCE_REVISION}"

BREW_PREFIX="$(brew --prefix)"
SDL2="$(brew --prefix sdl2)/lib/libSDL2-2.0.0.dylib"
# Existing Macs may retain classic SDL2 under its former Homebrew formula.
test -f "$SDL2" || SDL2="$BREW_PREFIX/opt/sdl2/lib/libSDL2-2.0.0.dylib"
ICON="${ROOT}/resources/brand/rcl.icns"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

APP="${STAGE}/Retrocycles RCL.app"
CONTENTS="${APP}/Contents"
MACOS="${CONTENTS}/MacOS"
FRAMEWORKS="${CONTENTS}/Frameworks"
RESOURCES="${CONTENTS}/Resources"
DOCUMENTATION="${RESOURCES}/Documentation"
mkdir -p "$MACOS" "$FRAMEWORKS" "$RESOURCES" "$DOCUMENTATION"

cp "$BINARY" "${MACOS}/armagetronad"
chmod +x "${MACOS}/armagetronad"
python3 "$ROOT/scripts/bundle-macos-libs.py" "${MACOS}/armagetronad" "$FRAMEWORKS" "$SDL2"
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
Source base revision: ${SOURCE_REVISION}
Corresponding source: ${SOURCE_URL}
EOF

for data_dir in config language models replays sound textures; do
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
  textures/ui/space-grotesk.fnt \
  replays/menu_fort.rclreplay \
  textures/floor.png \
  textures/title.png; do
  if test ! -f "${RESOURCES}/${required_file}"; then
    echo "error: required app resource not packaged: ${required_file}" >&2
    exit 1
  fi
done

clang -arch "$ARCH" -O2 -Wall -Wextra "$ROOT/scripts/macos-launcher.c" -o "${MACOS}/retrocycles-rcl"
MINIMUM_MACOS="$(python3 "$ROOT/scripts/macos_bundle_minimum.py" "$CONTENTS")"

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
  <key>LSMinimumSystemVersion</key><string>${MINIMUM_MACOS}</string>
  <key>RCLBuildID</key><string>${VERSION}</string>
  <key>RCLSourceRevision</key><string>${SOURCE_REVISION}</string>
  <key>RCLSourceURL</key><string>${SOURCE_URL}</string>
</dict>
</plist>
EOF

if $ADHOC; then
  printf 'Development build; ad-hoc signature, no Apple notarization.\n' >> "$DOCUMENTATION/SOURCE_INFO.txt"
  if test -n "$DIRTY"; then
    printf 'Local changes present; revision identifies the base source.\n' >> "$DOCUMENTATION/SOURCE_INFO.txt"
    git -C "$ROOT" diff --binary HEAD > "$DOCUMENTATION/LOCAL_CHANGES.patch"
  fi
  bash "$ROOT/scripts/sign-macos-app.sh" "$APP" --adhoc
else
  bash "$ROOT/scripts/sign-macos-app.sh" "$APP"
fi
python3 "$ROOT/scripts/verify-macos-app.py" "$APP"
if $NOTARIZE; then
  mkdir -p "$OUT_DIR"
  ditto -c -k --keepParent "$APP" "$STAGE/submission.zip"
  NOTARY_ARGS=()
  if test -n "${RCL_NOTARY_KEYCHAIN:-}"; then NOTARY_ARGS+=(--keychain "$RCL_NOTARY_KEYCHAIN"); fi
  xcrun notarytool submit "$STAGE/submission.zip" --keychain-profile "$RCL_NOTARY_PROFILE" "${NOTARY_ARGS[@]}" --wait --output-format json > "$OUT_DIR/notary-result.json"
  python3 - "$OUT_DIR/notary-result.json" <<'PYNOTARY'
import json, sys
if json.load(open(sys.argv[1])).get('status') != 'Accepted':
    raise SystemExit('Notarization rejected; inspect notary-result.json and retrieve submission log.')
PYNOTARY
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"
  spctl --assess --type execute --verbose "$APP"
fi
plutil -lint "${CONTENTS}/Info.plist" >/dev/null

mkdir -p "$OUT_DIR"
if test -e "$FINAL_APP"; then
  echo "error: output app already exists; move or remove it first: $FINAL_APP" >&2
  exit 1
fi
cp -R "$APP" "$FINAL_APP"

if test -e "$ARCHIVE" || test -e "${ARCHIVE}.sha256"; then
  echo "error: output archive already exists; move or remove it first: $ARCHIVE" >&2
  exit 1
fi
ditto -c -k --sequesterRsrc --keepParent "$FINAL_APP" "$ARCHIVE"
shasum -a 256 "$ARCHIVE" > "${ARCHIVE}.sha256"

echo "App:     $FINAL_APP"
echo "Archive: $ARCHIVE"
