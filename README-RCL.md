# Retrocycles RCL client and server

Retrocycles League's protocol-compatible Armagetron Advanced distribution.
The `main` branch carries the modern player client and the existing RCL
dedicated-server extensions on the **sty+ct+ap** gameplay baseline.

Current release line: **`0.2.9+sty+ct+ap+rcl`**.

- **Repo:** https://github.com/retrocyclesleague/armagetronad-rcl
- **Version policy:** [docs/VERSION_POLICY.md](docs/VERSION_POLICY.md)
- **Client product architecture:** [docs/CLIENT_ARCHITECTURE.md](docs/CLIENT_ARCHITECTURE.md)
- **Upstream developer guide:** [README-DEVELOPER](README-DEVELOPER)

## Quick build (dedicated server)

```bash
bash .cursor/scripts/cloud-agent-install.sh
bash scripts/smoke-dedicated.sh
```

Binary ends up under `build/` (symlink `build/armagetronad-dedicated` after `make run` layout, or `build/src/armagetronad-dedicated`).

## RCL patches on this line

- `FORCE_TURN` / `FORCE_BRAKE` — external AI control (Sumo Dojo)
- `AI_THINK` — disable built-in AI think loop when external script drives bots
- `PING_CHARITY_MIN` default 100 (was 0)

## Dashboard cross-links

When co-located on the UK host, dashboard docs live at `/data/rcl/rcl-dashboard/docs/`:

- `ARMAGETRON_SOURCE_REFERENCE.md` — AI, ladderlog, SPAWN_SCRIPT, sty+ct extras
- `SERVER_COMMANDS.md` — setting names and availability by version

## Cursor Cloud Agents

Environment: `.cursor/environment.json` + `.cursor/Dockerfile`. Set `CLOUD_AGENT_SKIP_BUILD=true` for doc-only tasks to skip compile on startup.
