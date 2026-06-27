# Dev log

## 2026-06-27 — Windows SDL3 port (`windows-sdl3`)

- Branch `windows-sdl3` from RCL `macos-sdl3`; MSYS2 autotools build via `scripts/build-windows-client.sh`.
- `configure.ac`: SDL3/sdl3-image via pkg-config; music off when `sdl3-mixer` missing; mingw winsock/GLU fixes.
- Link fixes: `-Wl,-u,WinMain`, `--start-group` around static libs (post-configure Makefile patch in build script).
- Source: `SDL3/SDL_main.h` + `main()` in `gArmagetron.cpp`; `InitGL`/`ExitGL` stubs when `DIRTY` off; `#undef SearchPath` before config enum (Win32 macro clash); `DATADIR` macro skipped on `_WIN32` in `acinclude.m4`.
- Output: `build-client/src/armagetronad_main.exe` (~25 MB debug build).

- Menu mouse hit: center on `YPos` (was offset down by `0.55×height`); row half `0.48×pitch` so adjacent rows don't overlap. Removed 70ms hover debounce.

## 2026-06-27 — Metal renderer audit: fixed primitive expansion slop

- `metalRenderer::FlushBatch` had copy-paste bugs that corrupted 3D geometry (floors/walls/cycles/zones use these):
  - **Triangle strips** were drawn as a flat triangle list (`expanded = batch_`) — wrong topology. Now expanded to a list with alternating winding.
  - **Triangle fans** used `i-1+j` from `i=2`, producing the wrong first triangle **and** an out-of-bounds read on the last vertex. Now `(0,i,i+1)` for `i` in `1..n-2`.
  - **Quad strips** reused the independent-quad triangulation, ignoring the `i,i+1,i+3,i+2` strip order. Now triangulated correctly.
- Collapsed the per-primitive copy loops into one `emit()` lambda.
- Removed dead `lastMatrix_` member + `MatrixMode()` shim (set, never read).
- `sr_MetalUploadTexture`: removed `rgba ? 4*width : 4*width` (identical branches).

### Known Metal gaps (not slop, deferred)
- Texture wrap (`repx_`/`repy_`) not propagated for tiled floors/walls — Metal upload returns before the GL wrap calls; only fonts set repeat via intercepted `glTexParameteri`. Floors may clamp instead of tile.
- No backface cull / smooth-shade / polygon-offset in Metal pipeline (`ReallySetFlag` ignores them).
- `sr_MetalDrawInternal` allocates a fresh `MTLBuffer` per batch (no reuse/ring buffer).
- `rMetalRender.mm`/`rMetalBackend.mm` are Xcode-only; autotools links `rMetalStub.cpp`.

- Server browser: fixed row hit geometry (`ItemDrawY`/`ItemRowHalf`, closest-row pick); 70ms mouse-hover debounce on all menus. Removed hover text scaling (looked wrong).

## 2026-06-27 — Menu shortcut badge layout fix

- Badges moved to far-left gutter (`x ≈ -0.9`), smaller font, neutral grey via `rTextField` colors (no selection tint).
- Server browser skips badge draw (`gServerMenu::ShowItemShortcuts` false); keys 1–9 still work.

## 2026-06-27 — Menu mouse + number shortcuts (all menus)

- Every `uMenu` shows `[1]`–`[9]` on rows (top item = 1). Keys 1–9 activate matching rows; mouse hover selects, click activates.
- Cursor shown and relative mouse disabled while any menu is open; restored on exit.

## 2026-06-27 — Revert RCL menu chrome on title + in-game ESC

- Removed `MainMenu.SetRclLayout()` in `gGame.cpp` so title screen and in-game escape menu use classic Armagetron menu styling again (`uRclLayout_Off` default). `uRclTheme` module remains for future use.

## 2026-06-27 — Metal font quality + SDL3 audio

- **Fonts on Metal:** `rFont.cpp` now uses `TexVertex`/`Vertex`/`Color` (was raw `glVertex2f`/`glTexCoord2f` — bypassed `metalRenderer`). Metal uploads textures without incomplete mip chains (`mipmapped:NO`), linear sampler + repeat-on-S for font atlas wrap, force `TEX_FONT` to `GL_LINEAR` on Metal reset.
- **Sound:** `eSound.cpp` opens playback via `SDL_OpenAudioDeviceStream` + `fill_audio` → `SDL_PutAudioStreamData`; lock/pause/destroy use SDL3 stream APIs. Sets `audio.freq` from stream format for mix rate math.

## 2026-06-27 — Metal GL compat infinite recursion crash (verified fix)

- Root cause: `rGL.h` macros made `sr_metal_glDisable` → `glDisable` → `sr_metal_glDisable` (stack overflow). Also triggered when Metal config was set but OpenGL fallback ran without a GL context.
- Fix: `real_gl*` wrappers in `rMetalGLCompat.cpp` call OpenGL directly; `#undef` macro block; `rMetalBackend.h` no longer pulls `rGL.h`.
- Xcode client: `DATA_DIR` + `RCL_XCODE_METAL` in preprocessor defs so the `.app` finds repo data.
- `./build-macos.sh` opens the built `.app` automatically.

## 2026-06-27 — Metal GL compat infinite recursion crash

- `rMetalGLCompat.cpp` no longer includes `rGL.h` (gl* macros caused `sr_metal_glDisable` → `glDisable` → `sr_metal_glDisable` stack overflow on macOS).

## 2026-06-27 — make -C src run / autotools vs Xcode Metal

- Removed `.mm` from `Makefile.am` (autotools cannot OBJCXX); added `rMetalStub.cpp` for autotools/Linux. Metal only in Xcode (`RCL_XCODE_METAL`).
- Added `run` target to `src/Makefile.am`; use `./build-macos.sh` then `make -C src run`.

## 2026-06-27 — macOS Metal Phase B (metalRenderer)

- `metalRenderer` implements `rRenderer` with batched Metal draws (lines/triangles/quads/strips/fans).
- `rMatrixState` + `rMetalGLCompat` intercept `glMatrixMode`/`glViewport`/`glEnable` on macOS when Metal backend active.
- Metal texture upload in `rTexture.cpp`; `ARMAGETRON_GRAPHICS_BACKEND 1` enables Metal on macOS (no `RCL_METAL_SMOKE` needed).

## 2026-06-27 — macOS Metal migration Phase A

- Added `docs/MACOS-MODERN.md` roadmap (SDL3 done → Metal rRenderer → textures → direct GL migration → SDL3 audio → notarized release).
- New `rGraphicsBackend` + `rMetalBackend.mm`: OpenGL vs Metal window flags, context lifetime, `sr_PresentFrame()`. Default remains OpenGL; `RCL_METAL_SMOKE` + `ARMAGETRON_GRAPHICS_BACKEND 1` for Metal clear-frame smoke test.

## 2026-06-23 — Auth/queue: armaauth only (no `/api/v1/client/*`)

- Client integration will use existing `/armaauth/0.1?query=…` via `nKrawall::FetchURL`, not Supabase JWT or new dashboard REST routes.
- Auth: `username@rcl` + game password through standard Krawall `check` flow; queue join/state/leave via RCL's extended armaauth queries (spec from dashboard `app/armaauth/[version]/route.ts`).
- Planned modules: thin `src/rcl/rclArmaAuth.cpp` wrapper around FetchURL + response parsing; console `RCL_AUTH` / `RCL_QUEUE` commands.

## 2026-06-23 — RCL menu layout fix

- Fixed main-menu soup: only selected item shows inline help, labels left-aligned, menu column kept left of sidebar (no overlap with feed/identity panels). Submenus back to classic UI until styled.
- Root cause of sidebar text in the wrong column: `DisplayText(center=1)` treats x as the right edge, so sidebar strings anchored at x≈0.09 extended left to x≈-0.7. Replaced with true left-edge `rTextField` drawing; strip locale color codes (`COLOR_IGNORE`); yellow column divider at x=-0.02.
- Menu/sidebar text doubling: `rTextField` was wrapping within narrow `SetWidth`, stacking multiple lines at the same Y. Now single-line with clip/ellipsis; row pitch increased to 0.125.
- Still busted: clip math truncated labels (`> Player .`); sidebar Y coords crossed box bounds. Rewrote to `DisplayText(center=-1)` left-edge layout, explicit per-line Y inside each sidebar box, footer on three x columns.

## 2026-06-23 — RCL UI theme (menus)

- New `uRclTheme` module: grid background, bracket selection, header/sidebar/footer chrome on main menu.
- All menus default to RCL layout (`>` prefixes, inline help, mouse + 1–9 shortcuts); main menu uses full chrome, in-game escape uses menu layout over dimmed game.

## 2026-06-23 — RCL in-game escape menu UI

- `uMenu::SetRclTheme(true)` on gameplay escape menu only: black panel, yellow border, yellow text, inverted selection row.
- Mouse hover/click, number keys 1–9 (top item = 1), cursor shown while menu open; relative mouse restored on exit.

## 2026-06-23 — macOS startup crash (wrong DATA_DIR)

- Crash at launch traced to Xcode DerivedData build from `armagetronad-tom11w-macos` worktree (missing `language/languages.txt`). RCL `./build-macos.sh` build runs fine.
- Added startup check for `language/languages.txt` with clear message; guard null `english` in locale fallback.

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
