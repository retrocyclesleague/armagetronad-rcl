#!/usr/bin/env bash
# Sign nested code inside out. Developer ID is required unless --adhoc is explicit.
set -euo pipefail
APP="${1:?Usage: sign-macos-app.sh APP [--adhoc]}"
IDENTITY="${RCL_SIGNING_IDENTITY:-}"
FLAGS=(--force --options runtime --timestamp)
if test "${2:-}" = --adhoc; then
  IDENTITY=-
  FLAGS=(--force --timestamp=none)
else
  case "$IDENTITY" in 'Developer ID Application: '*) ;; *) echo 'Set RCL_SIGNING_IDENTITY to a Developer ID Application identity.' >&2; exit 1;; esac
fi
if test -n "${RCL_SIGNING_KEYCHAIN:-}" && test "${RCL_SIGNING_SCOPE_ACTIVE:-}" != 1; then
  # --keychain alone can still select a duplicate private key in login. Scope the
  # user search list/default to the caller-unlocked keychain, restoring both.
  python3 - "$0" "$@" <<'PYKEYCHAIN'
import os, shlex, signal, subprocess, sys

def security(*args):
    return subprocess.check_output(['security', *args], text=True)

original_list = shlex.split(security('list-keychains', '-d', 'user'))
original_default = shlex.split(security('default-keychain', '-d', 'user'))
if len(original_default) != 1:
    raise SystemExit('Cannot safely capture the original default keychain')
keychain = os.environ['RCL_SIGNING_KEYCHAIN']
def interrupted(signum, frame):
    raise SystemExit(128 + signum)
for signum in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
    signal.signal(signum, interrupted)
result = 1
try:
    security('list-keychains', '-d', 'user', '-s', keychain)
    security('default-keychain', '-d', 'user', '-s', keychain)
    environment = dict(os.environ, RCL_SIGNING_SCOPE_ACTIVE='1')
    result = subprocess.run(['bash', *sys.argv[1:]], env=environment).returncode
finally:
    # Attempt both restorations even if one fails; a restore error fails signing.
    restored = True
    for args in [('default-keychain', '-d', 'user', '-s', *original_default),
                 ('list-keychains', '-d', 'user', '-s', *original_list)]:
        try:
            security(*args)
        except subprocess.CalledProcessError:
            restored = False
    if not restored:
        raise SystemExit('Could not restore the original user keychain settings')
raise SystemExit(result if result >= 0 else 128 - result)
PYKEYCHAIN
  exit $?
fi
if test "$IDENTITY" != -; then
  security find-identity -v -p codesigning ${RCL_SIGNING_KEYCHAIN:+"$RCL_SIGNING_KEYCHAIN"} | grep -F "\"$IDENTITY\"" >/dev/null || {
    echo 'Developer ID identity and private key are unavailable in the keychain.' >&2; exit 1;
  }
fi
if test -n "${RCL_SIGNING_KEYCHAIN:-}"; then
  FLAGS+=(--keychain "$RCL_SIGNING_KEYCHAIN")
fi
# Sign Mach-O leaves first, including codec libraries nested in SDL frameworks.
while IFS= read -r -d '' code; do
  if file -b "$code" | grep -q 'Mach-O'; then
    codesign "${FLAGS[@]}" --sign "$IDENTITY" "$code"
  fi
done < <(find "$APP/Contents" -type f -print0)
while IFS= read -r -d '' framework; do
  codesign "${FLAGS[@]}" --sign "$IDENTITY" "$framework"
done < <(find "$APP/Contents" -depth -type d -name '*.framework' -print0)
codesign "${FLAGS[@]}" --sign "$IDENTITY" "$APP"
codesign --verify --deep --strict "$APP"
