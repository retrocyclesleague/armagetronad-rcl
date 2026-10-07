#!/usr/bin/env bash
# Import ephemeral CI credentials; always remove the temporary keychain and P12.
set -euo pipefail
: "${RCL_CERTIFICATE_P12:?Missing RCL_MACOS_CERTIFICATE_P12 secret}"
: "${RCL_CERTIFICATE_PASSWORD:?Missing RCL_MACOS_CERTIFICATE_PASSWORD secret}"
: "${RCL_SIGNING_IDENTITY:?Missing RCL_MACOS_SIGNING_IDENTITY secret}"
: "${RCL_APPLE_ID:?Missing RCL_APPLE_ID secret}"
: "${RCL_APPLE_TEAM_ID:?Missing RCL_APPLE_TEAM_ID secret}"
: "${RCL_APPLE_APP_PASSWORD:?Missing RCL_APPLE_APP_PASSWORD secret}"
TEMP="$(mktemp -d)"
KEYCHAIN="$TEMP/release.keychain-db"
PASSWORD="$(openssl rand -hex 24)"
ORIGINAL="$(security default-keychain -d user | tr -d '"' | xargs)"
cleanup() {
  security default-keychain -d user -s "$ORIGINAL" || true
  security delete-keychain "$KEYCHAIN" || true
  rm -rf "$TEMP"
}
trap cleanup EXIT
printf '%s' "$RCL_CERTIFICATE_P12" | base64 --decode > "$TEMP/certificate.p12"
security create-keychain -p "$PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$PASSWORD" "$KEYCHAIN"
security default-keychain -d user -s "$KEYCHAIN"
security import "$TEMP/certificate.p12" -k "$KEYCHAIN" -P "$RCL_CERTIFICATE_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PASSWORD" "$KEYCHAIN" >/dev/null
export RCL_NOTARY_PROFILE=rcl-release
export RCL_SIGNING_KEYCHAIN="$KEYCHAIN"
export RCL_NOTARY_KEYCHAIN="$KEYCHAIN"
xcrun notarytool store-credentials "$RCL_NOTARY_PROFILE" --keychain "$KEYCHAIN" \
  --apple-id "$RCL_APPLE_ID" --team-id "$RCL_APPLE_TEAM_ID" --password "$RCL_APPLE_APP_PASSWORD"
bash scripts/package-macos-app.sh --notarize
