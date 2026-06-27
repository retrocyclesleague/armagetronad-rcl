#!/bin/bash
# Build Armagetron Advanced on macOS via Xcode (SDL3).
# Produces Armagetron Advanced.app — Tom11w macos0.2.9.3.0 SDL3 port integrated for RCL.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
MACOS="${ROOT}/MacOS"
CONFIG="${1:-Debug}"
DERIVED="${ROOT}/build/macos-xcode"
APP="${DERIVED}/Build/Products/${CONFIG}/Armagetron Advanced.app"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

need_brew() {
    if ! command -v brew >/dev/null; then
        echo "Homebrew is required: https://brew.sh" >&2
        exit 1
    fi
}

ensure_brew_deps() {
    local missing=()
    for pkg in libpng; do
        brew list "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if ((${#missing[@]})); then
        echo "Installing Homebrew dependencies: ${missing[*]}"
        brew install "${missing[@]}"
    fi
}

ensure_sdl_frameworks() {
    if test -d "${MACOS}/Frameworks/SDL3.framework"; then
        return 0
    fi
    echo "SDL3 frameworks missing — running MacOS/setup_fat_libs.sh (one-time download)..."
    bash "${MACOS}/setup_fat_libs.sh" || {
        echo "Note: fat libpng step may fail without x86 Homebrew; SDL frameworks should still install." >&2
        test -d "${MACOS}/Frameworks/SDL3.framework" || exit 1
    }
}

need_xcode() {
    if ! xcodebuild -version >/dev/null 2>&1; then
        echo "Xcode is required (App Store)." >&2
        exit 1
    fi
}

build_client() {
    mkdir -p "${DERIVED}"
    xcodebuild \
        -project "${MACOS}/Armagetron Advanced.xcodeproj" \
        -scheme "Armagetron Advanced" \
        -configuration "${CONFIG}" \
        -destination 'platform=macOS' \
        -derivedDataPath "${DERIVED}" \
        -jobs "${JOBS}" \
        build
}

install_run_symlink() {
    mkdir -p "${ROOT}/src"
    local bin="${APP}/Contents/MacOS/Armagetron Advanced"
    if test -x "${bin}"; then
        ln -sf "${bin}" "${ROOT}/src/armagetronad_main"
        echo "  Symlink:       ${ROOT}/src/armagetronad_main -> .app binary"
    fi
}

need_brew
need_xcode
ensure_brew_deps
ensure_sdl_frameworks
build_client
install_run_symlink

echo
echo "Build complete."
echo "  App bundle:    ${APP}"
echo "  Open the app:  open \"${APP}\""
echo
open "${APP}"
