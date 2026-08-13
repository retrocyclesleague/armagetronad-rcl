# AGENTS.md — armagetronad-rcl

Custom Armagetron Advanced build for [Retrocycles League](https://retrocyclesleague.com). Agents: read this before editing C++ or build files.

## Stack

- **Language:** C++ (autotools: `configure.ac`, `Makefile.am`)
- **Branch:** `main` (default; protocol-compatible sty+ct+ap line)
- **Display version:** `0.2.9+sty+ct+ap+rcl` (`major_version`)
- **Targets:** player client plus `armagetronad-dedicated` (`--disable-glout`)

## Setup (exact commands)

```bash
# Full build (also used by Cursor Cloud Agent install hook)
bash .cursor/scripts/cloud-agent-install.sh

# Smoke test (5s dedicated start)
bash scripts/smoke-dedicated.sh

# Before commit (when build system touched)
make -C build devcheck
```

Skip compile on Cloud Agent startup (doc-only tasks):

```bash
export CLOUD_AGENT_SKIP_BUILD=true
```

## Build from scratch (manual)

```bash
./bootstrap.sh
mkdir -p build && cd build
../configure --disable-glout DEBUGLEVEL=3 CODELEVEL=2
make -j"$(nproc)"
cd ..
bash scripts/smoke-dedicated.sh
```

## Client beta (player builds)

See [docs/CLIENT_BETA.md](docs/CLIENT_BETA.md). CI: `.github/workflows/build-client.yml`.

```bash
# Linux client
bash scripts/build-linux-client.sh && bash scripts/smoke-client.sh

# macOS client
bash build-macos.sh
CLIENT_BIN=src/armagetronad_main bash scripts/smoke-client.sh
```

Developer flags (`README-DEVELOPER`): `DEBUGLEVEL=3`, `CODELEVEL=2` on configure.

## Key directories

| Path | Purpose |
|------|---------|
| `src/tron/` | Game logic, cycles, AI, zones |
| `src/engine/` | ePlayer, ladderlog, sensors |
| `src/render/` | SPAWN_SCRIPT console |
| `config/` | Example configs, aiplayers |
| `batch/make/version` | Version string generation |

## RCL-specific server commands (this fork)

| Command | Purpose |
|---------|---------|
| `FORCE_TURN <player> <-1\|1>` | Turn player cycle (dojo / external AI) |
| `FORCE_BRAKE <player> <0\|1>` | Set cycle braking |
| `AI_THINK <0\|1>` | Enable/disable built-in AI think loop |

## Version policy

See [docs/VERSION_POLICY.md](docs/VERSION_POLICY.md).

- **Always** on default branch: protocol-compatible changes only
- **Never** bump protocol or remove fleet settings without a `rcl/0.3` branch plan
- **Tag** releases: `v0.2.9+sty+ct+ap+rcl.N`

## Always do

- Build dedicated server after C++ changes (`bash .cursor/scripts/cloud-agent-install.sh`)
- Run smoke test before opening PR
- Update `docs/VERSION_POLICY.md` changelog when tagging
- Link dashboard doc updates if behavior affects ops (`rcl-dashboard/docs/ARMAGETRON_SOURCE_REFERENCE.md`)

## Ask first

- Merging from upstream GitLab `trunk` (0.4 / SDL2)
- Changes to network protocol or auth (KRAWALL)
- Replacing production Docker images on tron-utils fleet

## Never do

- Commit secrets, `.env`, or compiled binaries under `docker/build/context/debian/bin/`
- Force-push `main` or `hack-0.2.8-sty+ct+ap`
- Build client-only deps on CI unless adding a client job explicitly
- Deploy untagged binaries to ranked pickup without ops sign-off

## Cursor Cloud specific instructions

- Environment: `.cursor/environment.json` (Ubuntu 22.04, autotools, libzthread)
- Optional sibling repo: `github.com/jamie-legg/rcl-dashboard` for docs/templates
- On UK host, dashboard path: `/data/rcl/rcl-dashboard`
- Fleet compose: `/data/tron-utils/` (separate repo; restart via admin agent)

## Commit messages

Short imperative: `add FORCE_TURN for dojo AI`, `fix ladderlog line for gridpos`. Reference Linear issue when applicable.
