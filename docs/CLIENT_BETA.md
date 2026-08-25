# RCL client beta program

Parallel dev channel for **player builds** of `armagetronad-rcl`. Steam Retrocycles remains the public default; team beta is opt-in.

The target product, trust boundaries, play-while-queued flow, and staged delivery
plan are defined in [CLIENT_ARCHITECTURE.md](CLIENT_ARCHITECTURE.md).

The shipped v1 signs in through the existing ArmaAuth game authority and queues
through authenticated `/add` on an RCL lobby. The lobby's mode-scoped bridge,
not the player client, calls the dashboard queue API. Client builds must never
embed `QUEUE_API_KEY`, an ingest key, service-role credentials, or browser
cookies.

**Linear project:** [RCL Client (Beta)](https://linear.app/retrocyclesleague/project/rcl-client-beta-fcc879adf357)

## Version tags

- Client + fleet: `v0.2.9+sty+ct+ap+rcl.N` (protocol-compatible with sty+ct+ap)
- Changelog: `docs/VERSION_POLICY.md`

## CI

Workflow: `.github/workflows/build-client.yml`

- **macOS (Apple Silicon)** — `build-macos.sh` + `scripts/package-macos-app.sh` → `Retrocycles-RCL-{version}-macos-arm64.zip`
- **Linux** — `scripts/build-linux-client.sh` → `Retrocycles-RCL-{version}-linux-x86_64.tar.gz`
- **Windows** — `scripts/build-windows-client.sh` (MSYS2 MINGW64) → `Retrocycles-RCL-{version}-windows-x86_64.tar.gz`
- Tag push `v0.2.9+sty+ct+ap+rcl.*` → draft GitHub prerelease. Publish only after platform QA, signing, and release approval.

Manual dispatch: Actions → **build-client** → Run workflow.

## Local build

```bash
# macOS
bash build-macos.sh
CLIENT_BIN=src/armagetronad_main bash scripts/smoke-client.sh
bash scripts/package-macos-app.sh
# Run: dist/Retrocycles RCL.app

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
  "latest": "rcl.4",
  "version": "0.2.9+sty+ct+ap+rcl.4",
  "released": "2026-08-13",
  "notes": "Modern protocol-compatible RCL client foundation",
  "builds": {
    "macos-arm64": {
      "url": "https://resource.retrocyclesleague.com/rcl/client/beta/Retrocycles-RCL-0.2.9+sty+ct+ap+rcl.4-macos-arm64.zip",
      "sha256": "..."
    },
    "linux-x86_64": {
      "url": "https://resource.retrocyclesleague.com/rcl/client/beta/Retrocycles-RCL-0.2.9+sty+ct+ap+rcl.4-linux-x86_64.tar.gz",
      "sha256": "..."
    },
    "windows-x86_64": {
      "url": "https://resource.retrocyclesleague.com/rcl/client/beta/Retrocycles-RCL-0.2.9+sty+ct+ap+rcl.4-windows-x86_64.tar.gz",
      "sha256": "..."
    }
  }
}
```

Upload via dashboard resource API — see `rcl-dashboard/docs/RESOURCE_UPLOAD.md`.

## Smoke checklist (~15 min, every build)

Post results in Discord `#rcl-client-dev` with build ID (`rcl.N`):

1. Launch cold; About shows `0.2.9+sty+ct+ap+rcl.N`
2. On a fresh profile, confirm setup returns to the main menu without forcing a local game
3. On a fresh profile, sign in with the RCL game username/password prompt (or cancel and confirm guest play remains available)
4. Select **Play Now → Sumobar** and confirm it joins an online, non-full RCL regional lobby without another login prompt
5. Select **Queue Now → Sumobar**; after silent server authentication, confirm `/add` is sent and the queue count is shown
6. Repeat Play Now for **Fort** and **TST**
7. Play one full round — no disconnect
8. Alt-tab + Discord stream — no focus/GL regression
9. Server browser — RCL gradient names visible
10. Map loads from `resource.retrocyclesleague.com`
11. Disconnect → Internet Play: no hour-long "Master servers do not answer" freeze (RCL master first; timeout is console-only)
12. Restart the client and confirm the saved RCL credential is validated before the main menu, with no password prompt; linked legacy IDs still authenticate normally through Player Setup

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

- `main` — protocol-compatible RCL client and server source (default)
- `hack-0.2.8-sty+ct+ap` — retained legacy baseline
- `rcl/**` — scoped experimental work before it is ready for `main`

Beta builds **never** deploy to ranked pickup fleet; team connects manually.
