# Cleanup plan (macOS dev focus)

Guided by [ponytail](https://github.com/DietrichGebert/ponytail): delete before you add, use what you already have.

## Canonical macOS path

```bash
./build-macos.sh          # Xcode + SDL3 frameworks → Armagetron Advanced.app
make -C src run           # symlinked binary, DATA_DIR from repo root
```

One-time: `brew install libpng` and `bash MacOS/setup_fat_libs.sh` (downloads SDL3 `.framework` bundles).

Autotools/SDL 1.2 path is legacy; macOS dev is Xcode-only on branch `macos-sdl3`.

## Tom11w macOS branch (SDL3 / Xcode)

Upstream fork: [Tom11w/armagetronad `macos0.2.9.3.0`](https://gitlab.com/Tom11w/armagetronad/-/tree/macos0.2.9.3.0)

22 commits on **v0.2.9.3.0** (not our `hack-0.2.8-sty+ct+ap` base). Xcode-first; autotools untouched.

### What's in it

| Area | Change |
|------|--------|
| **SDL 3** | Full migration: window/GL context, events, input, audio, textures |
| **Retina / menu gaps** | `sr_screenWidth/Height` from drawable pixels (`SDL_GetWindowSizeInPixels`) |
| **Multi-monitor** | Window created on correct display before fullscreen; `sr_lastDisplayIndex` |
| **Wall corner gaps** | `gWall.cpp` simple_trail fix (ported into this tree) |
| **Xcode** | Modern project, `MacOS/README.md`, `setup_fat_libs.sh`, release DMG scheme |

### Build Tom's branch (local worktree)

```bash
# Already fetched as tom11w/macos0.2.9.3.0; worktree at:
cd ../armagetronad-tom11w-macos/MacOS

# One-time: SDL3 frameworks (Homebrew headers alone are not enough — linker wants .framework)
bash setup_fat_libs.sh   # arm64-only OK; skips fat libpng without x86 Homebrew

# Open in Xcode → Armagetron Advanced scheme → My Mac → Run
# Or CLI:
xcodebuild -scheme "Armagetron Advanced" -configuration Debug -destination 'platform=macOS' build
```

Binary: `~/Library/Developer/Xcode/DerivedData/.../Debug/Armagetron Advanced.app`

### Integration status (RCL `macos-sdl3` branch)

- **Done:** SDL3 render/input from Tom11w; RCL game modes preserved; `./build-macos.sh` uses Xcode; wall corner gap + server browser crash fixes retained.
- **Worktree** `../armagetronad-tom11w-macos` kept as upstream reference; no longer required to build RCL.

## Phases

### Phase 1 — done

- Ponytail + project rules in `.cursor/rules/`
- Removed 9 root CodeBlocks `.cbp` projects
- Removed empty stubs: `src/leftover.cpp`, `src/prototype.cpp`, `src/engine/eKrawall.*`
- Aligned `configure_for_bundle.sh` with dev configure flags
- Removed stale SDL2 copy from `build_bundle.sh.in` (binary is SDL 1.2)

### Phase 2 — dependencies (next)

| Problem | Fix |
|---------|-----|
| `_deps/libxml2` vendored for `xmlNanoHTTP` | Move HTTP to libcurl; use Homebrew libxml2 for map XML only |
| `_deps/SDL_image` | Keep vendored until SDL2 migration (no Homebrew 1.2 formula) |
| ZThread | Replace with `std::thread` / pthread when auth is re-enabled |

### Phase 4 — Metal renderer (started)

See [MACOS-MODERN.md](MACOS-MODERN.md). Phase A landed: `rGraphicsBackend`, `rMetalBackend.mm` (present stub), `sr_PresentFrame()`. Default build stays OpenGL; define `RCL_METAL_SMOKE` in Xcode + `ARMAGETRON_GRAPHICS_BACKEND 1` to test Metal window clear.

### Phase 3 — structural (later)

- SDL 1.2 → SDL 2 (touches all of `src/render/`, input, audio)
- Optional: trim unused `docker/` deploy targets
- Optional: autotools → CMake (only after deps stabilize)

## Do not delete lightly

- `src/network/` — live multiplayer protocol and server browser
- `nKrawall.cpp` / `nAuthentication.cpp` — client login still references them
- Master/dedicated server code — ecosystem dependency even if you only build the client

## Commands

| Task | Command |
|------|---------|
| Rebuild client | `./build-macos.sh` |
| Run from tree | `make -C src run` |
| Review diff for bloat | Ask agent to run ponytail-style review on current changes |
