# Building Armagetron Advanced on macOS

This directory contains the Xcode project for macOS builds of both the game
client and the dedicated server.

## Prerequisites

- **Xcode** (from the App Store)
- **Homebrew** — [brew.sh](https://brew.sh)
- **libpng** from Homebrew:
  ```
  brew install libpng
  ```

## Quick start — single architecture (arm64 or x86_64)

1. Open `Armagetron Advanced.xcodeproj` in Xcode.
2. Select the **Armagetron Advanced** scheme and **My Mac** as the destination.
3. Build and run.

This builds for your Mac's native architecture using Homebrew's SDL3 and
libpng. No extra setup needed.

## Universal binary (arm64 + x86_64)

A universal binary requires fat versions of the third-party libraries that
Homebrew only ships for the host architecture. Run the setup script once to
download the official SDL3 frameworks and build a fat libpng:

```
bash MacOS/setup_fat_libs.sh
```

### What the script does

- Downloads the official SDL3, SDL3\_image, and SDL3\_mixer macOS frameworks
  from the SDL project's GitHub releases. These are already universal binaries
  with all image/audio format dependencies bundled inside.
- Copies the macOS framework slice out of each xcframework into
  `MacOS/Frameworks/` and re-signs it ad-hoc so macOS loads it without a
  code-signing error.
- Creates a fat `libpng.dylib` in `MacOS/Libs/` by combining the arm64
  Homebrew library (`/opt/homebrew/lib`) with the x86\_64 Homebrew library
  (`/usr/local/lib`) using `lipo`.

`MacOS/Frameworks/` and `MacOS/Libs/` are gitignored — each developer runs
the script locally.

### x86\_64 Homebrew (required for fat libpng)

If you do not already have the x86\_64 Homebrew at `/usr/local`, install it
once under Rosetta:

```
arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
arch -x86_64 /usr/local/bin/brew install libpng
```

### Building the universal binary

After running the setup script:

1. In Xcode, change the destination from **My Mac** to
   **Any Mac (arm64, x86\_64)**.
2. Build normally.

## Debug builds

Xcode sets `DATA_DIR` to the repository root (`$(SOURCE_ROOT)/..`). Open the
project from **this repo** (`armagetronad/MacOS/`), not the old `armagetronad-tom11w-macos`
worktree. If the app crashes on launch with a locale error, run **Product → Clean
Build Folder** or use `./build-macos.sh` from the repo root instead.

## Dedicated server

Select the **Armagetron Advanced Dedicated** scheme in Xcode. The dedicated
build has no SDL or OpenGL dependency and runs headless. The Debug
configuration sets `DATA_DIR` to the repository root so the server can find
language files when run from Xcode; the Release configuration expects to run
from inside a DMG distribution package.

## Release builds

Switch the Xcode scheme to **Release** for optimised (`-O2`/`-O3`) binaries.
The **Create Full Release** scheme builds a distributable DMG for the client
and a standalone binary for the dedicated server.
