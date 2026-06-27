# macOS modern stack (SDL3 → Metal → release)

Branch: `macos-sdl3`. Canonical build: `./build-macos.sh` → Xcode + SDL3 frameworks.

## Current state

| Layer | Status |
|-------|--------|
| **SDL3** window, input, events, Retina drawable size | Done (Tom11w port) |
| **OpenGL** rendering via `SDL_GL_CreateContext` | Default; `GL_SILENCE_DEPRECATION` on Apple |
| **`rRenderer` / `glRenderer`** | Partial — immediate-mode wrapper in `rGLRender.cpp` |
| **Direct OpenGL** in gameplay | Large — `gCycle.cpp`, `eDisplay.cpp`, `rTexture.cpp`, `rFont.cpp`, … |
| **Display lists** | `rDisplayList.cpp` — GL-only; default off on macOS |
| **SDL3 audio** | Incomplete — `eSound.cpp` has ponytail stubs (`SDL_OpenAudio` removed in SDL3) |
| **Universal binary** | `MacOS/setup_fat_libs.sh` + “Any Mac” destination |
| **Signing / notarization** | Ad-hoc dev sign only; no hardened runtime / notarization yet |

## Target stack

1. **SDL3** — window, HiDPI, fullscreen, multi-monitor, input, GameController, audio (SDL3_mixer).
2. **Metal** — native renderer on macOS (not MoltenVK unless we later want cross-platform Vulkan).
3. **Release** — universal `.app`, Developer ID sign, hardened runtime, notarized DMG.

Gameplay, networking, physics, zones, ladderlog: **do not rewrite**.

## Refactor order

### Phase A — Graphics context abstraction (done)

### Phase B — Metal `rRenderer` (done)

- `metalRenderer` in `rMetalRender.mm` — batched primitives, MVP + texture matrix shaders.
- `rMatrixState` + `rMetalGLCompat` — matrix/viewport/state compat for direct `gl*` calls on Metal.
- Metal textures in `rTexture.cpp`; set `ARMAGETRON_GRAPHICS_BACKEND 1` on macOS to use Metal.

### Phase C — Textures & fonts (next)

- `rTexture.cpp` — Metal textures instead of `glGenTextures` / `glTexImage2D`.
- `rFont.cpp` — glyph atlas as Metal texture.
- Drop display lists; use static Metal buffers or re-record each frame (AA scenes are small).

### Phase D — Direct GL migration

Priority files (rough GL call counts):

| File | Notes |
|------|--------|
| `gCycle.cpp` | Trails, cycles — highest traffic |
| `eDisplay.cpp` | World draw orchestration |
| `gZone.cpp`, `gWall.cpp` | Zones, walls |
| `gHudMap.cpp`, `gHud.cpp` | HUD |
| `rViewport.cpp` | Matrices, clip |
| `rModel.cpp` | Models — client arrays |
| `thirdparty/particles/opengl.cpp` | Particle backend switch |

Each file: route through `rRenderer` or a small Metal-specific helper; no new abstraction layers.

### Phase E — SDL3 audio

- Rewrite `eSound.cpp` for SDL3 audio streams / SDL3_mixer device API.
- Remove ponytail no-ops on lock/pause/close.

### Phase F — macOS release

- Xcode: `CODE_SIGN_IDENTITY`, hardened runtime, entitlements minimal (no JIT unless needed).
- `notarytool submit` on Release `.app` / DMG.
- Keep universal binary optional (arm64-only acceptable if Intel support dropped).

## Config keys (macOS)

```
ARMAGETRON_GRAPHICS_BACKEND 0   # 0=OpenGL, 1=Metal (experimental)
```

Metal smoke test (Xcode build only):

```
ARMAGETRON_GRAPHICS_BACKEND 1
```

Autotools `make` builds use OpenGL only; Metal requires `./build-macos.sh` (Xcode).

## Testing

```bash
./build-macos.sh Debug
make -C src run
```

Metal smoke (when `ARMAGETRON_GRAPHICS_BACKEND 1` and Metal window path enabled):

- Expect clear-frame only until Phase B; do not ship `1` to users until Phase C+.

## References

- [Tom11w macOS SDL3 branch](https://gitlab.com/Tom11w/armagetronad/-/tree/macos0.2.9.3.0)
- [SDL3 Metal](https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView)
- Apple: OpenGL deprecated — adopt Metal for graphics-intensive apps
