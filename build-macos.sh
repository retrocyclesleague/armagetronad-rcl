#!/bin/bash
# Build the native Retrocycles RCL client binary on macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="${ROOT}/_deps"
PREFIX="${ROOT}/_inst"
JOBS="${RCL_BUILD_JOBS:-4}"
case "$JOBS" in 1|2|3|4) ;; *) echo 'RCL_BUILD_JOBS must be between 1 and 4.' >&2; exit 1;; esac
command -v brew >/dev/null || { echo 'Homebrew is required.' >&2; exit 1; }
BREW_PREFIX="$(brew --prefix)"
PNG_PREFIX="${BREW_PREFIX}/opt/libpng"
# Reuse a caller-selected cache without tying the build to another checkout.
if test -n "${RCL_MACOS_DEPS_SOURCE:-}" && ! test -d "$DEPS/lib"; then
    ditto "$RCL_MACOS_DEPS_SOURCE" "$DEPS"
    python3 - "$RCL_MACOS_DEPS_SOURCE" "$DEPS" <<'PYRELOCATE'
import pathlib, sys
old, new = sys.argv[1:]
for pattern in ('*.pc', '*.la'):
    for path in pathlib.Path(new).rglob(pattern):
        path.write_text(path.read_text().replace(old, new))
PYRELOCATE
fi

export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig:${BREW_PREFIX}/lib/pkgconfig:${BREW_PREFIX}/opt/libxml2/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CPPFLAGS="-I${DEPS}/include/SDL -I${DEPS}/include/libxml2 ${CPPFLAGS:-}"
export LDFLAGS="-L${DEPS}/lib ${LDFLAGS:-}"

need_brew() {
    if ! command -v brew >/dev/null; then
        echo "Homebrew is required: https://brew.sh" >&2
        exit 1
    fi
}

ensure_brew_deps() {
    local missing=()
    for pkg in autoconf automake libtool pkg-config sdl12-compat libpng; do
        brew list "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if ((${#missing[@]})); then
        echo "Installing Homebrew dependencies: ${missing[*]}"
        brew install "${missing[@]}"
    fi
}

sdl_image_works() {
    # ImageIO backend on modern macOS returns empty surfaces; libpng must be linked in.
    test -f "${DEPS}/lib/libSDL_image.dylib" || return 1
    otool -L "${DEPS}/lib/libSDL_image.dylib" 2>/dev/null | grep -q libpng || return 1
    return 0
}

build_sdl12_addons() {
    mkdir -p "${DEPS}"
    cd /tmp
    for archive in SDL_image-1.2.12; do
        lib="${archive%-*}"
        if test "${lib}" = SDL_image && sdl_image_works; then
            continue
        fi
        test "${lib}" != SDL_image && test -f "${DEPS}/lib/lib${lib}.dylib" && continue
        echo "Building ${archive}..."
        curl -fsSL "https://www.libsdl.org/projects/${lib}/release/${archive}.tar.gz" -o "${archive}.tar.gz"
        rm -rf "${archive}"
        tar -xzf "${archive}.tar.gz"
        (
            cd "${archive}"
            if test "${lib}" = SDL_image; then
                CFLAGS="-Wno-incompatible-function-pointer-types" \
                ./configure --prefix="${DEPS}" --with-sdl-prefix="${BREW_PREFIX}" \
                    --disable-imageio --enable-png --disable-png-shared \
                    PKG_CONFIG_PATH="${BREW_PREFIX}/lib/pkgconfig" \
                    CPPFLAGS="-I${BREW_PREFIX}/include/SDL -I${PNG_PREFIX}/include/libpng16" \
                    LDFLAGS="-L${BREW_PREFIX}/lib -L${PNG_PREFIX}/lib"
            else
                ./configure --prefix="${DEPS}" --with-sdl-prefix="${BREW_PREFIX}"
            fi
            make -j"${JOBS}"
            make install
        )
        rm -rf "${archive}" "${archive}.tar.gz"
    done
}

build_libxml2() {
    mkdir -p "${DEPS}"
    if nm "${DEPS}/lib/libxml2.a" 2>/dev/null | grep xmlNanoHTTPOpen >/dev/null; then
        return 0
    fi
    echo "Building libxml2 2.14.5 with HTTP support..."
    cd /tmp
    local archive=libxml2-2.14.5
    curl -fsSL "https://download.gnome.org/sources/libxml2/2.14/${archive}.tar.xz" -o "${archive}.tar.xz"
    rm -rf "${archive}"
    tar -xf "${archive}.tar.xz"
    (
        cd "${archive}"
        ./configure --prefix="${DEPS}" --without-python --without-icu \
            --disable-shared --enable-static --with-http
        make -j"${JOBS}"
        make install
    )
    rm -rf "${archive}" "${archive}.tar.xz"
}

bootstrap_if_needed() {
    if test ! -x "${ROOT}/configure"; then
        echo "Running bootstrap.sh..."
        (cd "${ROOT}" && ./bootstrap.sh)
    fi
}

configure_and_build() {
    cd "${ROOT}"
    progtitle="Retrocycles RCL" \
    ./configure \
        --disable-binreloc \
        --disable-restoreold \
        --enable-automakedefaults \
        --disable-useradd \
        --disable-sysinstall \
        --disable-initscripts \
        --disable-uninstall \
        --disable-etc \
        --disable-games \
        --disable-armathentication \
        --disable-music \
        --prefix="${PREFIX}" \
        DEBUGLEVEL="${RCL_DEBUGLEVEL:-0}" \
        CODELEVEL="${RCL_CODELEVEL:-0}" \
        "$@"

    # Refresh commit-derived version even when reusing a configured tree.
    rm -f src/nTrueVersion.h
    make -C src -j"${JOBS}" armagetronad_main
    make -C resource included
}

need_brew
ensure_brew_deps
build_sdl12_addons
build_libxml2
bootstrap_if_needed
configure_and_build "$@"

echo
echo "Build complete."
echo "  Client binary: ${ROOT}/src/armagetronad_main"
echo "  Run from tree: make run"
echo "  Install to:    ${PREFIX} (make install)"
echo "  Package .app:  bash scripts/package-macos-app.sh"
