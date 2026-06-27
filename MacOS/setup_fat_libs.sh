#!/bin/bash
# Sets up build dependencies for universal (arm64 + x86_64) Xcode builds.
#
# - Downloads official SDL3 universal xcframeworks (SDL3, SDL3_image, SDL3_mixer)
# - Copies the macOS inner .framework out, strips quarantine, re-signs ad-hoc
# - Creates a fat libpng from dual Homebrew installs
#
# x86_64 Homebrew (once, for fat libpng):
#   arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
#   arch -x86_64 /usr/local/bin/brew install libpng
set -e

ARM_BREW=/opt/homebrew
X86_BREW=/usr/local
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBS_DIR="$SCRIPT_DIR/Libs"
FRAMEWORKS_DIR="$SCRIPT_DIR/Frameworks"

# --- SDL3 official universal xcframeworks ---

download_sdl_xcframework() {
    local repo="$1" name="$2"
    local xcfw="$FRAMEWORKS_DIR/${name}.xcframework"
    local fw="$FRAMEWORKS_DIR/${name}.framework"

    if [ ! -d "$xcfw" ]; then
        echo "  Fetching ${name} release info..."
        local dmg_url
        dmg_url=$(curl -sf "https://api.github.com/repos/libsdl-org/${repo}/releases/latest" \
            | python3 -c "
import sys, json
data = json.load(sys.stdin)
for a in data.get('assets', []):
    n = a['name']
    if n.endswith('.dmg') and 'debug' not in n.lower() and 'devel' not in n.lower():
        print(a['browser_download_url'])
        break
")
        if [ -z "$dmg_url" ]; then
            echo "  ERROR: could not find .dmg for ${name}" >&2; return 1
        fi

        local dmg="/tmp/${name}.dmg"
        echo "  Downloading $dmg_url"
        curl -L --progress-bar "$dmg_url" -o "$dmg"

        local mnt="/Volumes/${name}_install"
        hdiutil attach "$dmg" -mountpoint "$mnt" -quiet -nobrowse
        cp -R "$mnt"/*.xcframework "$FRAMEWORKS_DIR/"
        hdiutil detach "$mnt" -quiet
        rm -f "$dmg"
        echo "  ${name}.xcframework extracted"
    else
        echo "  ${name}.xcframework (already present)"
    fi

    # Copy (not symlink) the macOS inner framework so Xcode finds it directly.
    # Always refresh so stale symlinks or old copies get replaced.
    local inner
    inner=$(ls "$xcfw" | grep "^macos-" | head -1)
    if [ -z "$inner" ]; then
        echo "  ERROR: no macos- slice found in ${name}.xcframework" >&2; return 1
    fi
    rm -rf "$fw"
    cp -R "$xcfw/$inner/${name}.framework" "$fw"
    echo "  Copied ${name}.framework from xcframework/$inner/"

    # Strip quarantine and re-sign ad-hoc so macOS accepts the framework in dev builds
    xattr -dr com.apple.quarantine "$fw" 2>/dev/null || true
    codesign --force --deep --sign - "$fw" 2>/dev/null && \
        echo "  Re-signed ${name}.framework (ad-hoc)" || \
        echo "  Warning: codesign failed for ${name}.framework"
}

mkdir -p "$FRAMEWORKS_DIR"
echo "Setting up SDL3 frameworks in $FRAMEWORKS_DIR:"
download_sdl_xcframework SDL       SDL3
download_sdl_xcframework SDL_image SDL3_image
download_sdl_xcframework SDL_mixer SDL3_mixer

# --- Fat libpng (only remaining Homebrew dep) ---

if [ ! -x "$X86_BREW/bin/brew" ]; then
    echo ""
    echo "x86_64 Homebrew not found at $X86_BREW/bin/brew — needed for fat libpng."
    echo "Install it with:"
    echo "  arch -x86_64 /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    exit 1
fi

mkdir -p "$LIBS_DIR"
echo ""
echo "Building fat libpng in $LIBS_DIR:"

arm_png="$ARM_BREW/lib/libpng.dylib"
x86_png="$X86_BREW/lib/libpng.dylib"

if [ ! -f "$arm_png" ]; then
    echo "  MISSING arm64 libpng: brew install libpng" >&2; exit 1
fi
if [ ! -f "$x86_png" ]; then
    echo "  MISSING x86_64 libpng: arch -x86_64 $X86_BREW/bin/brew install libpng" >&2; exit 1
fi

out="$LIBS_DIR/libpng.dylib"
rm -f "$out"
lipo -create "$arm_png" "$x86_png" -output "$out"
install_name_tool -id "@rpath/libpng.dylib" "$out"
codesign --force --sign - "$out" 2>/dev/null || true
echo "  libpng.dylib"

echo ""
echo "Done. Set Xcode destination to 'Any Mac (arm64, x86_64)' and build."
