# Armagetron Advanced documentation index

## Building

- [macOS binary build](../build-macos.sh) — `./build-macos.sh` from the repo root (Xcode/SDL3 → `Armagetron Advanced.app`)
- [Windows client build](../scripts/build-windows-client.sh) — MSYS2 MINGW64 + SDL3 (`build-client/src/armagetronad_main.exe`)
- [Cleanup plan](CLEANUP.md) — ponytail-guided simplification, dependency upgrades, macOS dev focus
- [Developer notes](../README-DEVELOPER) — debug levels, `make run`, checks
- [Docker / release builds](../docker/README.md) — optional; Linux, Windows, macOS bundle packaging

## Runtime docs

- [macOS modern stack (SDL3 / Metal)](MACOS-MODERN.md) — Metal migration phases, config keys

Generated HTML docs live under `src/doc/` after a build. Starting point: `src/doc/index.html`.
