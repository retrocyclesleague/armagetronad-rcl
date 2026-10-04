#!/usr/bin/env bash
# Build Armagetron Advanced **client** on Windows (MSYS2 MINGW64).
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

build_zthread() {
  if test -f "${DEPS}/lib/libZThread.a" || test -f "${DEPS}/lib/libzthread.a"; then
    return 0
  fi

  echo "Building ZThread 2.3.2..."
  mkdir -p "${DEPS}"
  local work="/tmp/rcl-zthread-$$"
  mkdir -p "${work}"
  cd "${work}"

  # SourceForge redirects to a mirror; bound retries so CI cannot stall here.
  curl \
    --fail \
    --location \
    --connect-timeout 15 \
    --max-time 120 \
    --retry 4 \
    --retry-all-errors \
    --retry-delay 2 \
    --retry-max-time 300 \
    --output ZThread-2.3.2.tar.gz \
    "https://sourceforge.net/projects/zthread/files/ZThread/2.3.2/ZThread-2.3.2.tar.gz/download"
  printf '%s  %s\n' \
    950908b7473ac10abb046bd1d75acb5934344e302db38c2225b7a90bd1eda854 \
    ZThread-2.3.2.tar.gz | sha256sum -c -
  rm -rf ZThread-2.3.2
  tar -xzf ZThread-2.3.2.tar.gz
  (
    cd ZThread-2.3.2
    # Apply the semantic parts of Debian's long-maintained ZThread 2.3.2
    # GCC compatibility patches (020, 050, and 070). Keep this mechanical and
    # local so the exact upstream archive remains pinned and reproducible.
    sed -i.rcl-backup \
      -e '/^[[:space:]]*return false;[[:space:]]*$/d' \
      -e '/^[[:space:]]*return true;[[:space:]]*$/d' \
      -e 's/shareScope(\*this, extract(g))/shareScope(*this, this->extract(g))/' \
      -e 's/transferScope(\*this, extract(g))/transferScope(*this, this->extract(g))/' \
      -e 's/if(!isDisabled())/if(!LockHolder<LockType>::isDisabled())/' \
      include/zthread/Guard.h
    sed -i.rcl-backup \
      -e 's/ownerAcquired(self);/MutexImpl<List, Behavior>::ownerAcquired(self);/' \
      -e 's/waiterArrived(self);/MutexImpl<List, Behavior>::waiterArrived(self);/' \
      -e 's/waiterDeparted(self);/MutexImpl<List, Behavior>::waiterDeparted(self);/' \
      -e 's/ownerReleased(impl);/MutexImpl<List, Behavior>::ownerReleased(impl);/' \
      src/MutexImpl.h
    rm include/zthread/Guard.h.rcl-backup src/MutexImpl.h.rcl-backup
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
    ./configure --prefix="${DEPS}" --enable-shared=no
    make -j"${JOBS}"
    make install
  )
  # Leave the work directory before removing it. windres starts its
  # preprocessor through cmd.exe, which refuses to run from a directory that
  # no longer exists.
  cd "${ROOT}"
  rm -rf "${work}"
}

cd "${ROOT}"

if ! test -r configure; then
  ./bootstrap.sh
fi

build_zthread

# What configure is given. Make cannot tell that these changed, so a tree
# configured differently is cleaned and configured again.
RCL_TITLE="Retrocycles RCL"
BUILD_PROFILE="title=${RCL_TITLE};debug=${RCL_DEBUGLEVEL:-0};code=${RCL_CODELEVEL:-0}"
BUILD_PROFILE_STAMP="${BUILD}/.rcl-build-profile"

mkdir -p "${BUILD}"
if ! test -f "${BUILD}/Makefile" ||
    test "$(cat "${BUILD_PROFILE_STAMP}" 2>/dev/null || true)" != "${BUILD_PROFILE}"; then
  if test -f "${BUILD}/Makefile"; then
    make -C "${BUILD}" clean
  fi
  (
    cd "${BUILD}"
    progtitle="${RCL_TITLE}" \
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
      --disable-music \
      DEBUGLEVEL="${RCL_DEBUGLEVEL:-0}" \
      CODELEVEL="${RCL_CODELEVEL:-0}"
  )
  printf '%s\n' "${BUILD_PROFILE}" > "${BUILD_PROFILE_STAMP}"
fi

# The client carries the RCL icon as a resource, so Explorer, Alt-Tab and
# shortcuts show the same identity as the window.
"${WINDRES:-windres}" --include-dir "${ROOT}" \
  --input "${ROOT}/src/win32/rclClient.rc" --output "${BUILD}/rclClient-resource.o"

# The top-level all target regenerates command documentation by launching the
# GUI client with --doc; that process does not terminate under MSYS2 CI.
make -C "${BUILD}/src" -j"${JOBS}" armagetronad_main.exe \
  RCL_WINDOWS_RESOURCE=../rclClient-resource.o
make -C "${BUILD}/resource" included

# Build a native, statically linked GUI entry point for the packaged client.
# Keeping it free of MinGW runtime DLLs lets it live at the package root while
# the game and its dependency set remain together under bin/.
RCL_VERSION="$(sh "${ROOT}/batch/make/version" "${ROOT}")"
"${WINDRES:-windres}" \
  --include-dir "${ROOT}" \
  --define "RCL_VERSION_STRING=\\\"${RCL_VERSION}\\\"" \
  --input "${ROOT}/src/win32/rclLauncher.rc" \
  --output "${BUILD}/rclLauncher-resource.o"
"${CXX:-g++}" \
  -std=c++11 \
  -Os \
  -s \
  -mwindows \
  -static \
  -static-libgcc \
  -static-libstdc++ \
  -Wl,--no-insert-timestamp \
  -Wl,--dynamicbase,--nxcompat,--high-entropy-va \
  -o "${BUILD}/Retrocycles-RCL.exe" \
  "${ROOT}/src/win32/rclLauncher.cpp" \
  "${BUILD}/rclLauncher-resource.o" \
  -lshell32

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

echo "Client binary: ${BIN}"
echo "Client launcher: ${BUILD}/Retrocycles-RCL.exe"
