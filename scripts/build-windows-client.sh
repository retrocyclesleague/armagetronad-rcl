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
  rm -rf "${work}"
}

cd "${ROOT}"

if ! test -r configure; then
  ./bootstrap.sh
fi

build_zthread

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
      --disable-music \
      DEBUGLEVEL=3 \
      CODELEVEL=2
  )
fi

# The top-level all target regenerates command documentation by launching the
# GUI client with --doc; that process does not terminate under MSYS2 CI.
make -C "${BUILD}/src" -j"${JOBS}" armagetronad_main.exe
make -C "${BUILD}/resource" included

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
