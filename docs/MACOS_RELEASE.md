# macOS release of the Windows product line

Build from `rcl/ui-brief` (baseline `43d8c400`, 2026-10-06), which contains the
current Windows UI, menu replay, gameplay changes, models, textures and sounds.
`windows-sdl3` is an older, separate port and is not this product's release base.
The macOS build uses the same shared C++ source and assets with SDL1 compatibility
libraries. No UI, network, protocol or authentication rewrite is required.

## Local Apple Silicon build

Install Xcode command line tools and Homebrew. Then:

```sh
bash build-macos.sh
EXPECTED_VERSION="$(sh batch/make/version .)" \
  CLIENT_BIN=src/armagetronad_main bash scripts/smoke-client.sh
bash scripts/package-macos-app.sh --adhoc
open 'dist/Retrocycles RCL.app'
```

Release defaults match Windows: optimized code, `DEBUGLEVEL=0`, `CODELEVEL=0`,
optional SDL_mixer music disabled. Game sound effects remain enabled. Four build
jobs are the default; `RCL_BUILD_JOBS=1` through `4` changes that. A caller can reuse
an existing dependency cache with `RCL_MACOS_DEPS_SOURCE=/absolute/path/_deps`.
Native Intel builds use the Intel Homebrew prefix. Universal builds are not yet
supported or tested; package names describe the actual executable architecture.

Packaging includes the current RCL icon, fonts, menu replay, all sounds and game
resources. A native launcher locates resources relative to the app and uses
`~/Library/Application Support/Retrocycles RCL Client` by default. Explicit
`--userdatadir` arguments can select a temporary profile. Paths with spaces work.
The linked dylib closure is copied and rewritten; SDL2 is also bundled because
SDL12-compat loads it dynamically. On newer Homebrew installations SDL2 itself
may be a compatibility library; the packager detects and includes its dynamically
loaded SDL3 too. That fresh dependency chain still needs a clean CI runtime test. A deterministic verifier checks resources,
architecture slices, runtime search paths and library closure.

The default local mode (also selected by `--adhoc`) explicitly labels a development archive. It verifies local code integrity
but is not Developer ID distribution trust. Local changed source is marked as
such in `Documentation/SOURCE_INFO.txt` and a tracked-source patch is included.

## Developer ID and notarization

A release requires an installed **Developer ID Application** identity with its
private key. `security find-identity -v -p codesigning` must list it. Store Apple
credentials in the keychain, never in the repository:

For local signing, a dedicated unlocked keychain can be selected with
`RCL_SIGNING_KEYCHAIN=/absolute/path/to/rcl-signing.keychain-db`. Keep that
keychain and any password file outside the repository, with password-file mode
`0600`. The caller unlocks it before invoking the signer; the signer never reads
its password or changes private-key ACLs. To avoid duplicate identities in login,
it temporarily selects only this keychain in the user search list and as default,
then restores the exact previous list and default on success, failure or handled
interruption. This does not reset or modify the login keychain.

Store notarization credentials separately:

```sh
xcrun notarytool store-credentials rcl-release \
  --apple-id 'YOUR_APPLE_ID' --team-id 'YOUR_TEAM_ID'
```

Commit the release source, build it, then package into a fresh directory:

```sh
export RCL_SIGNING_IDENTITY='Developer ID Application: YOUR NAME (YOUR_TEAM_ID)'
export RCL_NOTARY_PROFILE=rcl-release
bash scripts/package-macos-app.sh --notarize --out-dir dist/release
```

The script signs Mach-O files inside out with hardened runtime and a secure
timestamp, verifies the complete app, submits a ZIP, requires `Accepted`, staples
and validates the ticket, and checks Gatekeeper. It then creates the final ZIP
and SHA256. No weakened library-validation or JIT entitlement is added. If Apple
rejects the submission, inspect `notary-result.json` and fetch its log with
`xcrun notarytool log SUBMISSION_ID --keychain-profile rcl-release`.

Tags and manual `sign_macos` workflow dispatch require these Actions secrets:

- `RCL_MACOS_CERTIFICATE_P12` — base64 PKCS#12 with certificate and private key
- `RCL_MACOS_CERTIFICATE_PASSWORD`
- `RCL_MACOS_SIGNING_IDENTITY` — full Developer ID Application identity
- `RCL_APPLE_ID`, `RCL_APPLE_TEAM_ID`, `RCL_APPLE_APP_PASSWORD`

CI imports them into a temporary keychain and removes the keychain and P12 on
exit. Missing credentials fail release signing; there is no ad-hoc fallback.
Branch builds produce development archives. Publishing remains a separate step.

## Focused QA

Run the verifier and signature check:

```sh
python3 scripts/verify-macos-app.py 'dist/Retrocycles RCL.app'
codesign --verify --deep --strict 'dist/Retrocycles RCL.app'
```

Launch a copy outside the checkout with a fresh profile using LaunchServices:

```sh
open -n '/path with spaces/Retrocycles RCL.app' --args \
  -window --userdatadir '/tmp/rcl-fresh-profile'
```

Verify title card, language selection, first setup, cancelable sign-in, compact
menus, proportional text, animated replay, mouse and keyboard navigation,
Cmd-Q/window close, sounds and a complete online round. Test Gatekeeper on
another Mac before public distribution. Compiling on the current Mac does not
establish support for older macOS releases; bundled Homebrew library deployment
requirements also apply.

The packager sets `LSMinimumSystemVersion` to the highest deployment target
declared by any bundled executable or library, and the verifier rejects a lower
declared requirement. The current local build and its Homebrew libraries require
macOS 26.0. Builds on the macOS 15 CI runner may declare a different minimum;
the packaged native code determines it. Supporting an older macOS requires
rebuilding every dependency with that deployment target, then testing there.

Apple references: [Developer ID](https://developer.apple.com/developer-id/) and
[custom notarization](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).
