# Dev log

## 2026-10-04 - title card, Windows launcher, default camera

Ported from the unpushed `codex/windows-first-run-config` checkout at Jamie's request, now that this branch is the product line.

- Title card: `scripts/generate-rcl-brand.py` writes the RCL splash (`textures/title.png`/`.jpg`), the window icon, `tron.ico`, an `.icns` and SVG sources under `resources/brand/`; its output is pixel-identical to that checkout's. `gLogo::Display` fills the window in graphite and contains the 2:1 artwork instead of stretching a 4:3 image. It shows for returning players at startup (up to 6 s, any key skips; the screenshot key does not) and lies over the menu replay. Before this, optimised builds showed the stock Armagetron Advanced title there.
- Launcher: `Retrocycles-RCL.exe` (`src/win32/rclLauncher.*`) replaces `Retrocycles-RCL.cmd` at the root of the Windows package; same profile directory, arguments passed on. The client carries the icon as a resource (`rclClient.rc`). `--rcl-check-install` validates the package layout and profile without a window; `scripts/smoke-client.sh` and CI run it against the extracted archive. Packaging checks that the launcher is a GUI executable depending only on Windows system DLLs.
- Camera: the default external camera is back 20, rise 20, pitch -0.75 (was 30 / 20 / -0.7), glance the same. These are settings, not saved per profile; an `autoexec.cfg` still overrides them. The server-defined camera is unchanged.

Checked on Windows: the title card at startup and the menu after it; a package built locally, extracted to a clean directory, `--rcl-check-install` passing, the client started through the launcher with a separate profile and quitting cleanly (launcher exits with it); the default camera values in the built client's settings dump. Not checked: the launcher's error dialogs, a profile path with non-ASCII characters, the `.icns` on macOS (nothing uses it yet).

## 2026-10-04 - interface brief: Space Grotesk, one component system, dark default

Work against `RCL_CLIENT_MODERNISATION_BRIEF.md` (phase 0 and the phase 1 menu slice, plus the parts of phase 2 that came with it). The description of the system is in `docs/CLIENT_ARCHITECTURE.md`, section Interface.

- Text: proportional Space Grotesk everywhere (`render/rUiFont.*`, atlases in `textures/ui/` from `scripts/generate-rcl-ui-font.py`), drawn texel for pixel. `RCL_UI_FONT 0` restores the bitmap font.
- Interface strings are lowercased when the English language file loads, not at draw time; user and server text keep their case. HUD literals lowercased by hand.
- `uRclTheme` rewritten: the kit's inverse palette with one lime (`#EEFF41`), logical-pixel layout, a navigation rail on the left, the same wordmark on every screen, rows with real controls (toggle, selector, slider, text field, key binding), dialogs with wrapped text. `uMenu` draws rows through it and handles hover, click, selector arrows and slider drags; rows end at the column's edge, so the pointer no longer selects from anywhere on the screen.
- Menus: play now is the main menu's primary action, resume the in-game menu's. Local Game is Practice, Exit Game is Quit, Player moved under Settings (player & controls), Extras and Config Files under Settings / Advanced. Escape on the main menu goes to Quit before it quits.
- Server browser: measured columns with right-aligned numbers, sort column lit in the header, selected server's details under the list, authored name colours kept but lifted where they would sink into the background.
- Number rows no longer clamp settings when a menu is built; unknown selector values show as "custom".
- Binding capture ignores mouse movement for the first 0.4 s, so the click that starts it cannot end it.
- Dialogs: a key with a global job (the screenshot key) does it instead of dismissing the message; words wider than a line (paths in About) wrap instead of being shortened.
- Arena: the dark grid arena and the original cycle are the default again. The pale arena and the lofted cycle body (`cycle_body_clean`) remain behind `RCL_CLEAN_ARENA` / Display & Graphics / Detail. The menu replay is drawn dark and quiet, with solid cycles on the floor.
- Product name: configure gets `progtitle="Retrocycles RCL"` on all three client build scripts, so the window title and `\g` in strings say so. The Windows script re-configures when its inputs change. Window icon is the wordmark.
- Fixed: `se_SoundExit` closed the audio device while holding the audio lock. Under sdl12-compat that hangs whenever the audio thread is waiting for the lock: at exit, and in optimised builds at startup, where sound is initialised twice. Before: 3 of 6 optimised launches hung there (stack in `se_SoundExit`); after: 0 of 51.
- Space Grotesk notice (SIL OFL 1.1) added to `THIRD_PARTY_NOTICES.md`; packaging requires the font metrics file.

Checked in the running Windows client (optimised build) at 1280x720 and 1920x1080: main menu, settings pages, key bindings, sign-in prompt, server browser, About dialog and its scrolling, in-game menu over a local game, HUD, quitting through the menu (the process ends). Keyboard throughout; pointer hover, click, toggle and selector stepping with synthetic events. Not checked: a physical mouse (slider dragging in particular), the chat and console prompts after the last layout changes, the binding-capture state on screen, first-run setup, split screen, ultrawide, fullscreen switches, online play, macOS and Linux clients beyond CI compiling them. Frame rate in a local 1080p game was in the same range with the new font and the old one (roughly 1050-1600 fps, uncapped, scenes not matched); that is not a frame-time study.
## 2026-10-04 - clean arena, new cycle body, HUD and menu controls

- Clean arena (`RCL_CLEAN_ARENA`, default on, toggle under Display Settings / Detail): pale open floor that fades into the sky colour instead of a grid, no rim walls (a low kerb marks the boundary, which still kills), a point light with a pool of light on the floor, solid trails with thickness, soft floor shadows, and floor reflections at a stronger sheen. Colours and the light live in `rScreen.cpp` and `eDisplay.cpp` (`se_CleanArenaLight`).
- Trails: the thick shell and the shadows are overlay passes on the existing wall renderer (`gWallRenderMode_Shadow`, `gWallRenderMode_Solid`), cached in the same display list as the glow. The drawn half width is .05; collision is unchanged on the centre plane.
- Cycle body: `scripts/generate-rcl-cycle-body.py` writes `models/cycle_body.mod` (lofted shell, 1122 vertices, replacing the 22-vertex body) and `textures/cycle_body.png` (player-coloured shell, pale fairing, canopy, headlight). `scripts/generate-rcl-textures.py` still has the old body texture as a source and would overwrite it. Cycle lights are neutral instead of red and blue.
- Text over the bright scene: bright text gets a dark drop shadow, the console plate is darker, and the HUD sits on a dark strip. HUD defaults drop the position, fastest and ping readouts.
- Menu replay drawn in the same look. Detail menu gains Anti-aliasing and Clean Arena.
- The "Turn Angle" and "deleting cycle" console lines are `#ifdef DEBUG` only; release builds, which CI now packages, do not print them.
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
