# RCL client beta program

Parallel dev channel for **player builds** of `armagetronad-rcl`. Steam Retrocycles remains the public default; team beta is opt-in.

**Linear project:** [RCL Client (Beta)](https://linear.app/retrocyclesleague/project/rcl-client-beta-fcc879adf357)

## Version tags

- Fleet + beta: `v0.2.9-sty+ct+ap+rcl.N` (protocol-compatible with sty+ct+ap)
- Changelog: `docs/VERSION_POLICY.md`

## CI

Workflow: `.github/workflows/build-client.yml`

- **macOS** — `build-macos.sh` → `Retrocycles-RCL-{version}-macos-universal.tar.gz`
- **Linux** — `scripts/build-linux-client.sh` → `Retrocycles-RCL-{version}-linux-x86_64.tar.gz`
- **Windows** — `scripts/build-windows-client.sh` (MSYS2 MINGW64) → `Retrocycles-RCL-{version}-windows-x86_64.tar.gz`
- Tag push `v0.2.9-sty+ct+ap+rcl.*` → GitHub Release (prerelease)

Manual dispatch: Actions → **build-client** → Run workflow.

## Local build

```bash
# macOS
bash build-macos.sh
CLIENT_BIN=src/armagetronad_main bash scripts/smoke-client.sh

# Linux
bash scripts/build-linux-client.sh
bash scripts/smoke-client.sh
bash scripts/package-client.sh \
  --binary build-client/src/armagetronad_main \
  --platform linux-x86_64
```

## Beta manifest (resource repo)

Published at:

`https://resource.retrocyclesleague.com/rcl/client/beta/manifest.json`

Example shape (`resources/client-beta/manifest.example.json`):

```json
{
  "latest": "rcl.1",
  "version": "0.2.9-sty+ct+ap+rcl.1",
  "released": "2026-06-13",
  "notes": "First team beta — version string + smoke pipeline",
  "builds": {
    "macos-universal": {
      "url": "https://resource.retrocyclesleague.com/rcl/client/beta/Retrocycles-RCL-0.2.9-sty+ct+ap+rcl.1-macos-universal.tar.gz",
      "sha256": "..."
    },
    "linux-x86_64": {
      "url": "https://resource.retrocyclesleague.com/rcl/client/beta/Retrocycles-RCL-0.2.9-sty+ct+ap+rcl.1-linux-x86_64.tar.gz",
      "sha256": "..."
    },
    "windows-x86_64": {
      "url": "https://resource.retrocyclesleague.com/rcl/client/beta/Retrocycles-RCL-0.2.9-sty+ct+ap+rcl.1-windows-x86_64.tar.gz",
      "sha256": "..."
    }
  }
}
```

Upload via dashboard resource API — see `rcl-dashboard/docs/RESOURCE_UPLOAD.md`.

## Smoke checklist (~15 min, every build)

Post results in Discord `#rcl-client-dev` with build ID (`rcl.N`):

1. Launch cold; About shows `0.2.9-sty+ct+ap+rcl.N`
2. Join an RCL pickup server (UK sumobar or TST)
3. `/login you@rcl` — auth works
4. Play one full round — no disconnect
5. Alt-tab + Discord stream — no focus/GL regression
6. Server browser — RCL gradient names visible
7. Map loads from `resource.retrocyclesleague.com`
8. Disconnect → Internet Play: no hour-long "Master servers do not answer" freeze (RCL master first; timeout is console-only)

## Known client pain: master list after DC

Stock Armagetron blocks the UI refreshing master servers over UDP after a disconnect. RCL beta fixes this — see `rcl-dashboard/docs/player/CLIENT_SERVER_BROWSER.md`. Until players are on RCL beta: **Direct Connect** or **favorites**.

## Filing bugs (Linear)

Label: **Client**. Include:

- Build: `rcl.N` or git tag
- OS + version
- Repro steps
- Expected vs actual
- `user.log` / console snippet if crash

## Branches

- `hack-0.2.8-sty+ct+ap` — protocol-safe patches (default)
- `rcl/client-beta` — experimental UI (queue menu, pop connect) — future

Beta builds **never** deploy to ranked pickup fleet; team connects manually.
