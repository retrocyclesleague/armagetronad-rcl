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
case "$VERSION" in
  ''|*[!0-9A-Za-z.+_-]*)
    echo "error: version contains characters unsafe for a package name: $VERSION" >&2
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
SOURCE_URL="${SOURCE_REPOSITORY}/tree/${SOURCE_REVISION}"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

PKG="Retrocycles-RCL-${VERSION}-${PLATFORM}"
PACKAGE_ROOT="${STAGE}/${PKG}"
mkdir -p "${PACKAGE_ROOT}/bin"

EXE_NAME="armagetronad"
if test "${BINARY##*.}" = exe || test "${PLATFORM}" = windows-x86_64; then
  EXE_NAME="armagetronad.exe"
fi

cp "$BINARY" "${PACKAGE_ROOT}/bin/${EXE_NAME}"
if test "${EXE_NAME}" != "armagetronad.exe"; then
  chmod +x "${PACKAGE_ROOT}/bin/${EXE_NAME}"
fi

BINARY_DIR="$(cd "$(dirname "$BINARY")" && pwd)"
BUILD_ROOT="$(cd "${BINARY_DIR}/.." && pwd)"

for document in COPYING.txt README-RCL.md THIRD_PARTY_NOTICES.md; do
  if test ! -f "${ROOT}/${document}"; then
    echo "error: distribution document not found: ${ROOT}/${document}" >&2
    exit 1
  fi
  cp "${ROOT}/${document}" "${PACKAGE_ROOT}/${document}"
done

# Keep the archive self-contained. These are the only source-tree directories
# the runtime is allowed to read from the package.
RUNTIME_DATA_DIRS="config language models sound textures"
for data_dir in ${RUNTIME_DATA_DIRS}; do
  if test ! -d "${ROOT}/${data_dir}"; then
    echo "error: runtime data directory not found: ${ROOT}/${data_dir}" >&2
    exit 1
  fi
  cp -R "${ROOT}/${data_dir}" "${PACKAGE_ROOT}/${data_dir}"
done

# Configure generates the runtime language index rather than tracking it.
# Overlay it from the build that produced this binary; an in-tree build has
# the same BUILD_ROOT as ROOT.
if test -f "${BUILD_ROOT}/language/languages.txt"; then
  cp "${BUILD_ROOT}/language/languages.txt" \
    "${PACKAGE_ROOT}/language/languages.txt"
fi

# Only bundled maps/DTDs belong in a release archive. The automatic resource
# cache is user-writable runtime state, and resource/proto is developer input.
# Out-of-tree builds generate this directory next to their built src/ tree;
# in-tree builds retain the traditional source-root location.
INCLUDED_RESOURCE_DIR="${BUILD_ROOT}/resource/included"
if test ! -d "$INCLUDED_RESOURCE_DIR"; then
  INCLUDED_RESOURCE_DIR="${ROOT}/resource/included"
fi
if test ! -d "$INCLUDED_RESOURCE_DIR"; then
  echo "error: included resources not found beside the build or source tree" >&2
  exit 1
fi
mkdir -p "${PACKAGE_ROOT}/resource"
cp -R "$INCLUDED_RESOURCE_DIR" "${PACKAGE_ROOT}/resource/included"

for required_file in \
  config/default.cfg \
  language/languages.txt \
  models/cycle_body.mod \
  resource/included/map.dtd \
  sound/cyclrun.wav \
  textures/title.png; do
  if test ! -f "${PACKAGE_ROOT}/${required_file}"; then
    echo "error: required runtime file not packaged: ${required_file}" >&2
    exit 1
  fi
done

if test "${PLATFORM}" = windows-x86_64; then
  if ! command -v ldd >/dev/null 2>&1; then
    echo "error: ldd is required to collect Windows runtime DLLs" >&2
    exit 1
  fi
  if ! command -v pacman >/dev/null 2>&1; then
    echo "error: pacman is required to identify MinGW DLL owners and licenses" >&2
    exit 1
  fi

  # Follow the full MinGW dependency graph. Windows system DLLs have no source
  # path in ldd output and are supplied by Windows; every resolved *.dll is
  # copied beside the executable. Fail rather than ship a partially runnable
  # client when a dependency cannot be resolved.
  DLL_QUEUE_FILE="${STAGE}/windows-dll-queue"
  DLL_DONE_FILE="${STAGE}/windows-dll-done"
  DLL_PACKAGE_FILE="${STAGE}/windows-dll-packages"
  printf '%s\n' "$BINARY" > "$DLL_QUEUE_FILE"
  : > "$DLL_DONE_FILE"
  printf '%s\n' \
    mingw-w64-x86_64-gcc-libs \
    mingw-w64-x86_64-libwinpthread > "$DLL_PACKAGE_FILE"
  while IFS= read -r dependency; do
    test -n "$dependency" || continue
    if grep -Fqx -- "$dependency" "$DLL_DONE_FILE"; then
      continue
    fi
    printf '%s\n' "$dependency" >> "$DLL_DONE_FILE"

    LDD_OUTPUT="$(ldd "$dependency" 2>&1)" || {
      echo "error: unable to inspect Windows runtime dependencies for: $dependency" >&2
      printf '%s\n' "$LDD_OUTPUT" >&2
      exit 1
    }
    if printf '%s\n' "$LDD_OUTPUT" | grep -E '[[:space:]]not found([[:space:]]|$)' >/dev/null; then
      echo "error: unresolved Windows runtime dependency for: $dependency" >&2
      printf '%s\n' "$LDD_OUTPUT" >&2
      exit 1
    fi

    printf '%s\n' "$LDD_OUTPUT" | awk '
      $2 == "=>" && $3 ~ /\.dll$/ { print $3 }
      $1 ~ /^\// && $1 ~ /\.dll$/ { print $1 }
    ' | while IFS= read -r dll; do
      test -f "$dll" || continue
      case "$dll" in
        /mingw*/bin/*.dll|/ucrt*/bin/*.dll|/clang*/bin/*.dll|/usr/bin/*.dll|"${ROOT}"/_deps-win/bin/*.dll)
          ;;
        */[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/*|*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]\\*|[A-Za-z]:\\[Ww][Ii][Nn][Dd][Oo][Ww][Ss]\\*)
          continue
          ;;
        *)
          echo "error: refusing to package DLL from an unexpected path: $dll" >&2
          exit 1
          ;;
      esac
      dll_name="${dll##*/}"
      if test ! -f "${PACKAGE_ROOT}/bin/${dll_name}"; then
        cp "$dll" "${PACKAGE_ROOT}/bin/${dll_name}"
        printf '%s\n' "$dll" >> "$DLL_QUEUE_FILE"
        owner_package="$(pacman -Qo "$dll" 2>/dev/null | awk '$2 == "is" && $3 == "owned" && $4 == "by" { print $5; exit }')"
        if test -z "$owner_package"; then
          echo "error: unable to identify the MSYS2 package that owns DLL: $dll" >&2
          exit 1
        fi
        printf '%s\n' "$owner_package" >> "$DLL_PACKAGE_FILE"
      fi
    done
  done < "$DLL_QUEUE_FILE"

  MINGW_LICENSE_DIR="${PACKAGE_ROOT}/ThirdPartyLicenses/MSYS2"
  DLL_PACKAGE_SORTED_FILE="${STAGE}/windows-dll-packages-sorted"
  sort -u "$DLL_PACKAGE_FILE" > "$DLL_PACKAGE_SORTED_FILE"
  mkdir -p "$MINGW_LICENSE_DIR"
  while IFS= read -r mingw_package; do
    test -n "$mingw_package" || continue
    PACKAGE_LICENSE_LIST="${STAGE}/${mingw_package}-licenses"
    pacman -Qlq "$mingw_package" 2>/dev/null | \
      grep -Ei '/(licenses?|copying)(/|$)' > "$PACKAGE_LICENSE_LIST" || true
    if test ! -s "$PACKAGE_LICENSE_LIST"; then
      echo "error: installed MSYS2 package has no discoverable license files: $mingw_package" >&2
      exit 1
    fi
    package_license_count=0
    while IFS= read -r license_path; do
      test -f "$license_path" || continue
      license_name="$(printf '%s' "$license_path" | sed 's|^/||; s|/|_|g')"
      cp "$license_path" "${MINGW_LICENSE_DIR}/${license_name}"
      package_license_count=$((package_license_count + 1))
    done < "$PACKAGE_LICENSE_LIST"
    if test "$package_license_count" -eq 0; then
      echo "error: MSYS2 package license entries are not regular files: $mingw_package" >&2
      exit 1
    fi
  done < "$DLL_PACKAGE_SORTED_FILE"

  cat > "${PACKAGE_ROOT}/Retrocycles-RCL.cmd" <<'EOF'
@echo off
setlocal
set "RCL_ROOT=%~dp0"
set "RCL_PROFILE=%APPDATA%\Retrocycles RCL Client"
if not exist "%RCL_PROFILE%" mkdir "%RCL_PROFILE%"
"%RCL_ROOT%bin\armagetronad.exe" --datadir "%RCL_ROOT%." --configdir "%RCL_ROOT%config" --userdatadir "%RCL_PROFILE%" %*
EOF
  RUN_LINE="  2. Run: Retrocycles-RCL.cmd (or double-click it after extracting)"
  EXAMPLE_DIR="C:\\Retrocycles-RCL"
  RUNTIME_NOTE="Runtime: required non-system MinGW DLLs are included in bin/. Windows supplies the remaining system libraries."
else
  cat > "${PACKAGE_ROOT}/retrocycles-rcl" <<'EOF'
#!/bin/sh
set -eu
RCL_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RCL_PROFILE="${XDG_DATA_HOME:-${HOME}/.local/share}/retrocycles-rcl-client"
mkdir -p "$RCL_PROFILE"
exec "${RCL_ROOT}/bin/armagetronad" \
  --datadir "${RCL_ROOT}" \
  --configdir "${RCL_ROOT}/config" \
  --userdatadir "$RCL_PROFILE" \
  "$@"
EOF
  chmod +x "${PACKAGE_ROOT}/retrocycles-rcl"
  RUN_LINE="  2. Run: ./retrocycles-rcl"
  EXAMPLE_DIR="~/Retrocycles-RCL"
  RUNTIME_NOTE="Runtime: this archive uses system glibc, libstdc++, SDL 1.2, SDL_image 1.2, OpenGL, libpng, libxml2, and ZThread libraries."
fi

cat > "${PACKAGE_ROOT}/README.txt" <<EOF
Retrocycles RCL client beta — ${VERSION} (${PLATFORM})

Install (side-by-side with Steam Retrocycles):
  1. Extract this archive anywhere (e.g. ${EXAMPLE_DIR})
${RUN_LINE}
  3. Verify About shows ${VERSION}

Smoke checklist + issue tracker:
  https://linear.app/retrocyclesleague/project/rcl-client-beta-fcc879adf357

Every bug report must include build ID: ${VERSION}
Steam Retrocycles remains the public default; this build is opt-in team beta.

${RUNTIME_NOTE}

License and corresponding source:
  License: COPYING.txt
  Third-party notices: THIRD_PARTY_NOTICES.md
  Source repository: ${SOURCE_REPOSITORY}
  Exact source revision: ${SOURCE_REVISION}
  Corresponding source: ${SOURCE_URL}
EOF

mkdir -p "$OUT_DIR"
ARCHIVE="${OUT_DIR}/${PKG}.tar.gz"
tar -C "$STAGE" -czf "$ARCHIVE" "$PKG"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$ARCHIVE" | tee "${ARCHIVE}.sha256"
else
  shasum -a 256 "$ARCHIVE" | tee "${ARCHIVE}.sha256"
fi
echo "Packaged: $ARCHIVE"
