# RCL version policy

## Lines

| Display version | Git branch | Protocol | Use |
|-----------------|------------|----------|-----|
| `0.2.9+sty+ct+ap+rcl.N` | `main` | Compatible with sty+ct+ap clients | **Default** — client, fleet, dojo, ranked |
| `0.3.0-rcl.N` | `rcl/0.3` (future) | Breaking | Beta servers, coordinated client rollout |
| `1.0.0-rcl` | `rcl/1.0` (future) | Product milestone | Stable Retrocycles-branded release |

## Rules

1. **Default branch** (`main`): protocol-compatible patches only. No removed settings, no protocol bumps. The legacy `hack-0.2.8-sty+ct+ap` branch remains available for history and fleet comparison.
2. **Breaking changes** go on `rcl/0.3` with `major_version` bumped and a migration note in this file.
3. **Tags** for immutable releases use `v0.2.9+sty+ct+ap+rcl.N`, beginning with `.4` so release versions remain newer than the `.3_alpha` development builds already distributed. The historical baseline remains tagged `v0.2.9-sty+ct+ap+rcl.0` under the old spelling.
4. **Docker tags** normalize `+` to `-` (for example `0.2.9-sty-ct-ap-rcl.4`) because the Docker tag grammar does not allow plus signs.
5. **Upstream trunk (0.4)** is not this repo’s default; cherry-pick only with an explicit issue.

## Version files

- `major_version` — public flavor string (currently `0.2.9+sty+ct+ap+rcl`)
- `minor_version` — suffix template for dev builds (see `batch/make/version`)
- Protected git tags override automatic version in CI

## Changelog

- **rcl.4** — Modern compatible client foundation: Retina rendering, HD fonts/materials, faithful cycle geometry, RCL menu system with mouse navigation, themed server browser, refreshed procedural audio, self-contained macOS packaging, and Linux/Windows package staging. Source moves to `main`; public binary release remains gated on platform QA and signing.
- **rcl.1–rcl.3** — Development identifiers only; no immutable Git release tags were published.
- **rcl.0** — First tagged RCL baseline: FORCE_TURN, FORCE_BRAKE, AI_THINK, PING_CHARITY_MIN=100, agent dev env.

After tagging `rcl.N`, immediately advance `minor_version` to `.N+1_alphaDATE`
on `main`. This keeps development snapshots newer than the last immutable
release and prevents configuration version downgrades.
