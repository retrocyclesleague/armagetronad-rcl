# RCL version policy

## Lines

| Display version | Git branch | Protocol | Use |
|-----------------|------------|----------|-----|
| `0.2.9-sty+ct+ap+rcl.N` | `hack-0.2.8-sty+ct+ap` | Compatible with sty+ct+ap clients | **Default** — fleet, dojo, ranked |
| `0.3.0-rcl.N` | `rcl/0.3` (future) | Breaking | Beta servers, coordinated client rollout |
| `1.0.0-rcl` | `rcl/1.0` (future) | Product milestone | Stable Retrocycles-branded release |

## Rules

1. **Default branch** (`hack-0.2.8-sty+ct+ap`): protocol-compatible patches only. No removed settings, no protocol bumps.
2. **Breaking changes** go on `rcl/0.3` with `major_version` bumped and a migration note in this file.
3. **Tags** for fleet deploys: `v0.2.9-sty+ct+ap+rcl.0`, `v0.2.9-sty+ct+ap+rcl.1`, …
4. **Docker images** should mirror git tags (e.g. `ghcr.io/retrocyclesleague/armagetronad-rcl:0.2.9-sty+ct+ap+rcl.0`).
5. **Upstream trunk (0.4)** is not this repo’s default; cherry-pick only with an explicit issue.

## Version files

- `major_version` — public flavor string (currently `0.2.9-sty+ct+ap+rcl`)
- `minor_version` — suffix template for dev builds (see `batch/make/version`)
- Protected git tags override automatic version in CI

## Changelog

- **rcl.0** — First tagged RCL baseline: FORCE_TURN, FORCE_BRAKE, AI_THINK, PING_CHARITY_MIN=100, agent dev env.
