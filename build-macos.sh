#!/bin/bash
# Build the native Retrocycles RCL client binary on macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="${ROOT}/_deps"
PREFIX="${ROOT}/_inst"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/libxml2/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
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
    for archive in SDL_image-1.2.12 SDL_mixer-1.2.12; do
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
                ./configure --prefix="${DEPS}" --with-sdl-prefix=/opt/homebrew \
                    --disable-imageio --enable-png --disable-png-shared \
                    PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig" \
                    CPPFLAGS="-I/opt/homebrew/include/SDL -I/opt/homebrew/opt/libpng/include/libpng16" \
                    LDFLAGS="-L/opt/homebrew/lib -L/opt/homebrew/opt/libpng/lib"
            else
                ./configure --prefix="${DEPS}" --with-sdl-prefix=/opt/homebrew
            fi
            make -j"${JOBS}"
            make install
        )
        rm -rf "${archive}" "${archive}.tar.gz"
    done
}

build_libxml2() {
    mkdir -p "${DEPS}"
    if nm "${DEPS}/lib/libxml2.a" 2>/dev/null | grep -q xmlNanoHTTPOpen; then
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
        --prefix="${PREFIX}" \
        "$@"

    make -j"${JOBS}"
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
