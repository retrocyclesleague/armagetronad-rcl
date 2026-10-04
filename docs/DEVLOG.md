# Dev log

## 2026-10-04 - map downloads on Windows, glow cache, anti-aliasing, floor reflections

- Map downloads: `tResourceManager` used libxml2's nanohttp, which MSYS2's libxml2 2.15 no longer has, so every map fetch failed on the Windows client. The plain HTTP client moved to `tools/tHttp` and resource downloads use it when `LIBXML_HTTP_ENABLED` is not defined.
- Trail glow: the glow of finished walls is cached in a display list next to the core wall list and rebuilt with it, instead of redrawing every segment in immediate mode each frame.
- Anti-aliasing: `RCL_ANTIALIAS` (samples per pixel, default 4, 0 = off) requests a multisampled GL visual and falls back to a plain one if the driver has none.
- Floor reflections: `FLOOR_MIRROR` defaults to cycles and walls on hardware renderers for new profiles. Existing profiles keep their setting.
## 2026-10-02 - menu replay, one menu style, Windows sign-in

- Menus: one panel style everywhere, palette taken from retrocyclesleague.com (neutral greys, `#e8ff47` accent) and kept in one place in `uRclTheme.cpp`. Menu copy is lowercased at draw time; text being edited is not. The label/value rule is only drawn on menus that have values. Help text wraps instead of being squeezed.
- Main menu: Play Now, Queue Now, RCL Account, Servers, Local Game, Player, Settings, About, Exit. Extras and Config Files moved under Settings.
- Menu background: `gMenuReplay.cpp` plays `replays/menu_fort.rclreplay` behind the out-of-game menus (overhead opening shot, then orbit, chase, high and zone-to-zone shots with blended hand-overs). `MENU_REPLAY 0` restores the old background. Regenerate the file with `scripts/generate-menu-replay.py`; it writes no names, chat or ids.
- Sign-in on Windows: MSYS2's libxml2 2.15 has no HTTP client, so `nKrawall::FetchURL` always failed and RCL sign-in reported "unavailable". Without `LIBXML_HTTP_ENABLED` it now does the plain HTTP GET itself with a 6 s budget. Builds whose libxml2 has HTTP are unchanged.
- Cancelling or failing the RCL Account prompt no longer overwrites the saved credential.
- Packaging: Windows archive is a `.zip`; client build scripts default to optimised builds (`RCL_DEBUGLEVEL=3 RCL_CODELEVEL=2` for the old debug build).
## 2026-06-13 — macOS binary build

- Added `build-macos.sh`: Homebrew deps, local `_deps/` (SDL 1.2 image/mixer, libxml2 2.14 with `--with-http`), configure, and `make`.
- Homebrew no longer ships `sdl_image`/`sdl_mixer` 1.2 or libxml2 with HTTP; both are built into `_deps/`.
- Client builds with `--disable-armathentication` (avoids ZThread). Binary: `src/armagetronad_main`.
- Run from build tree: `make run` (symlinks binary and uses local `var/` for config).
- Fixed black textures: SDL_image 1.2 must be built with `--disable-imageio --disable-png-shared` (ImageIO returns empty surfaces; dynamic `libpng.dylib` lookup fails on Homebrew).
