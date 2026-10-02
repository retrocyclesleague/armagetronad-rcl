#!/usr/bin/env bash
# Build Armagetron Advanced **client** on Windows (MSYS2 MINGW64, SDL3).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-client"
DEPS="${ROOT}/_deps-win"
JOBS="${NUMBER_OF_PROCESSORS:-4}"
MINGW_PREFIX="${MINGW_PREFIX:-/mingw64}"

export PATH="${MINGW_PREFIX}/bin:${DEPS}/bin:${PATH}"
export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig:${MINGW_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export CPPFLAGS="-I${DEPS}/include ${CPPFLAGS:-}"
export LDFLAGS="-L${DEPS}/lib ${LDFLAGS:-}"

require_sdl3() {
  if ! pkg-config --exists sdl3; then
    echo "error: SDL3 not found. Install MSYS2 packages:" >&2
    echo "  pacman -S mingw-w64-x86_64-sdl3 mingw-w64-x86_64-sdl3-image" >&2
    exit 1
  fi
  if ! pkg-config --exists sdl3-image; then
    echo "error: SDL3_image not found (pacman -S mingw-w64-x86_64-sdl3-image)" >&2
    exit 1
  fi
  echo "SDL3: $(pkg-config --modversion sdl3)"
  echo "SDL3_image: $(pkg-config --modversion sdl3-image)"
  if pkg-config --exists sdl3-mixer 2>/dev/null; then
    echo "SDL3_mixer: $(pkg-config --modversion sdl3-mixer)"
  else
    echo "SDL3_mixer: not installed (music disabled)"
  fi
}

build_zthread() {
  if test -f "${DEPS}/lib/libZThread.a" || test -f "${DEPS}/lib/libzthread.a"; then
    return 0
  fi

  echo "Building ZThread 2.3.2..."
  mkdir -p "${DEPS}"
  local work="/tmp/rcl-zthread-$$"
  mkdir -p "${work}"
  cd "${work}"

  curl -fsSL "https://sourceforge.net/projects/zthread/files/ZThread/2.3.2/ZThread-2.3.2.tar.gz/download" \
    -o ZThread-2.3.2.tar.gz
  rm -rf ZThread-2.3.2
  tar -xzf ZThread-2.3.2.tar.gz
  (
    cd ZThread-2.3.2
    # ZThread 2.3.2 ships ancient automake macros; strip the ones MSYS2 no longer ships.
    sed -i \
      -e 's/^AM_ACLOCAL_INCLUDE/# AM_ACLOCAL_INCLUDE/' \
      -e 's/^AM_DETECT_PTHREAD/# AM_DETECT_PTHREAD/' \
      -e 's/^AM_WITH_DOXYGEN/# AM_WITH_DOXYGEN/' \
      -e 's/^AM_ENABLE_ATOMIC_LINUX/# AM_ENABLE_ATOMIC_LINUX/' \
      -e 's/^AM_ENABLE_ATOMIC_GCC/# AM_ENABLE_ATOMIC_GCC/' \
      -e 's/^AM_DETECT_FTIME/# AM_DETECT_FTIME/' \
      configure.ac
    libtoolize --copy --force
    autoreconf -fi
    # ZThread 2.3.2 predates modern GCC; permissive mode required on GCC 13+.
    export CXXFLAGS="-fpermissive ${CXXFLAGS:-}"
    ./configure --prefix="${DEPS}" --enable-shared=no
    make -j"${JOBS}"
    make install
  )
  rm -rf "${work}"
}

cd "${ROOT}"

require_sdl3

if ! test -r configure; then
  ./bootstrap.sh
fi

build_zthread

# Reconfigure when switching SDL major versions (SDL2 → SDL3).
CONFIGURE_STAMP="${BUILD}/.configure-stamp"
CONFIGURE_ARGS="sdl3-$(pkg-config --modversion sdl3)-$(pkg-config --modversion sdl3-image)"
if ! test -f "${CONFIGURE_STAMP}" || test "$(cat "${CONFIGURE_STAMP}")" != "${CONFIGURE_ARGS}"; then
  rm -rf "${BUILD}"
fi

mkdir -p "${BUILD}"
if ! test -f "${BUILD}/Makefile"; then
  (
    cd "${BUILD}"
    ../configure \
      --prefix="${BUILD}/install" \
      --with-zthread-prefix="${DEPS}" \
      --disable-restoreold \
      --enable-automakedefaults \
      --disable-useradd \
      --disable-sysinstall \
      --disable-initscripts \
      --disable-uninstall \
      --disable-etc \
      --disable-games \
      --disable-armathentication \
      $(pkg-config --exists sdl3-mixer 2>/dev/null || echo --disable-music) \
      DEBUGLEVEL=3 \
      CODELEVEL=2
    echo "${CONFIGURE_ARGS}" > "${CONFIGURE_STAMP}"
  )
fi

# MinGW ld needs --start-group for circular static lib refs; automake cannot express this in LDADD.
MK="${BUILD}/src/Makefile"
if test -f "${MK}" && ! grep -q 'start-group.*armagetronad_main_OBJECTS' "${MK}"; then
  sed -i 's/\$(AM_V_CXXLD)\$(armagetronad_main_LINK) \$(armagetronad_main_OBJECTS) \$(armagetronad_main_LDADD) \$(LIBS)/$(AM_V_CXXLD)$(CXXLD) $(AM_CXXFLAGS) $(CXXFLAGS) $(armagetronad_main_LDFLAGS) $(LDFLAGS) -o $@ -Wl,--start-group $(armagetronad_main_OBJECTS) $(armagetronad_main_LDADD) $(LIBS) -Wl,--end-group/' "${MK}"
fi

make -C "${BUILD}" -j"${JOBS}" all debug

# languages.txt is generated in the build dir; copy into source tree for in-tree runs.
if test -f "${BUILD}/language/languages.txt"; then
  mkdir -p "${ROOT}/language"
  cp -f "${BUILD}/language/languages.txt" "${ROOT}/language/languages.txt"
fi

BIN=""
for candidate in \
  "${BUILD}/src/armagetronad_main.exe" \
  "${BUILD}/src/armagetronad_main" \
  "${BUILD}/armagetronad_main.exe"; do
  if test -f "$candidate"; then
    BIN="$candidate"
    break
  fi
done

if test -z "$BIN"; then
  echo "error: client binary not found under ${BUILD}" >&2
  exit 1
fi

deploy_runtime_dlls() {
  local dest_dir
  dest_dir="$(dirname "$BIN")"
  echo "Deploying runtime DLLs to ${dest_dir}..."
  # Copy MinGW runtime deps next to the exe so it runs outside MSYS2 PATH.
  ldd "$BIN" | awk '/\/mingw64\/bin\// {print $3}' | sort -u | while read -r dll; do
    cp -f "$dll" "${dest_dir}/"
  done
}

write_run_launcher() {
  local dest_dir launcher
  dest_dir="$(dirname "$BIN")"
  launcher="${dest_dir}/run-armagetronad.bat"
  cat > "${launcher}" <<EOF
@echo off
cd /d "%~dp0"
start "" "%~dp0armagetronad_main.exe" %*
EOF
  echo "Run launcher: ${launcher}"
}

deploy_runtime_dlls
write_run_launcher

echo "Client binary: ${BIN}"
echo "Tip: double-click run-armagetronad.bat or run the exe from build-client/src after rebuild."
