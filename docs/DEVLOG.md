# Dev log

## 2026-06-22 — Console multiline paste

- Pasting config with newlines into the in-game console (Cmd/Ctrl+V) now runs each line via `LoadAll` instead of stripping line breaks.

## 2026-06-22 — RCL default configs

- Client defaults in `config/settings_client.cfg`: zone height/segments/alpha, custom/glance camera, FOV 85, network rates 16384.
- New `config/keys_rcl.cfg` (turn left S/D/F, turn right J/K/L) as first-setup default; C++ fallbacks aligned in `gZone.cpp`, `eCamera.cpp`, `ePlayer.cpp`, `nNetwork.cpp`.

## 2026-06-22 — Tom11w SDL3 integrated into RCL macOS build

- Branch `macos-sdl3`: Tom11w `macos0.2.9.3.0` render/input/SDL3 stack merged into RCL fork; `./build-macos.sh` now drives Xcode (SDL3 frameworks via `MacOS/setup_fat_libs.sh`).
- Kept RCL game logic (`gGame`, `gCycle`, `gRotation`, `gRace`, etc.); patched SDL3 events in RCL files; restored RCL menus (`sg_ConfigMenu`, `sg_SpecialMenu`) on top of Tom's `gMenus.cpp`.
- Added RCL-only sources to Xcode project: `gRace`, `gHudMap`, `gRotation`, `gSvgOutput`, `eBannedWords`. Build succeeds → `build/macos-xcode/Build/Products/Debug/Armagetron Advanced.app`.

## 2026-06-22 — Tom11w macOS branch notes

- Fetched `tom11w/macos0.2.9.3.0` (22 commits on v0.2.9.3.0): SDL3 + SDL3_image/mixer via Xcode, Retina drawable-size fixes, multi-monitor window placement, input/event API updates.
- Worktree for Xcode builds: `../armagetronad-tom11w-macos` (open `MacOS/Armagetron Advanced.xcodeproj`).
- Ported wall corner gap fix (`gWall.cpp` simple_trail) into this tree — independent of SDL version.

## 2026-06-22 — ponytail cleanup phase 1 (macOS dev)

- Added `.cursor/rules/ponytail.mdc` and `armagetronad-cleanup.mdc`; documented plan in `docs/CLEANUP.md`.
- Deleted 9 legacy CodeBlocks `.cbp` projects and empty stubs (`leftover.cpp`, `prototype.cpp`, `eKrawall.*`).
- `configure_for_bundle.sh`: `--disable-armathentication` to match `build-macos.sh`.
- `build_bundle.sh.in`: removed stale SDL2 dylib copy (client is SDL 1.2).

## 2026-06-22 — server browser segfault on macOS

- Crash in `gServerMenu::Update()` when opening Internet/LAN server browser: null `gServerMenuItem*` after `dynamic_cast`, then `SetServer()` deref at offset 0x28.
- Root cause: `gServerMenuItem` destructor removed/re-added the last menu item (filter) while destroying a server row, leaving dangling/null slots; fixed by letting `~uMenuItem` handle removal. Added null guard before `SetServer()` in `Update()`.

## 2026-06-13 — macOS binary build

- Added `build-macos.sh`: Homebrew deps, local `_deps/` (SDL 1.2 image/mixer, libxml2 2.14 with `--with-http`), configure, and `make`.
- Homebrew no longer ships `sdl_image`/`sdl_mixer` 1.2 or libxml2 with HTTP; both are built into `_deps/`.
- Client builds with `--disable-armathentication` (avoids ZThread). Binary: `src/armagetronad_main`.
- Run from build tree: `make run` (symlinks binary and uses local `var/` for config).
- Fixed black textures: SDL_image 1.2 must be built with `--disable-imageio --disable-png-shared` (ImageIO returns empty surfaces; dynamic `libpng.dylib` lookup fails on Homebrew).
