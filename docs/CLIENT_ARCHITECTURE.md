# RCL client product architecture

## Product promise

The RCL client is a faithful, protocol-compatible Armagetron Advanced player
build with a modern mouse-first experience and first-class RCL competition
features.

A player can:

1. Sign in to RCL without giving the game a browser session or shared API key.
2. Join and leave eligible RCL queues from the client.
3. Enter a public server while the queue continues in the background.
4. See and hear a match pop, leave the public server cleanly, and connect to the
   assigned pickup server.
5. Configure the game through discoverable pointer, keyboard, and controller
   friendly menus without losing access to advanced console settings.

Gameplay, simulation, maps, camera behavior, and network compatibility remain
Armagetron Advanced. RCL owns the surrounding identity, matchmaking, release,
and competitive experience.

## Baseline and constraints

The first release line is based on the RCL `main` branch, retaining the
`hack-0.2.8-sty+ct+ap` gameplay and protocol baseline. This
keeps compatibility with the current pickup fleet and preserves the game that
players already know. The fork currently uses the legacy SDL 1.2/OpenGL client,
so it is a compatibility baseline rather than the final modernization target.

Moving the client to upstream `trunk`/SDL2 is a separate, explicitly approved
program. It must not be mixed into the queue/authentication work or silently
change the game protocol.

The game fork remains distributed under its existing GNU GPL terms. Release
packages must carry the applicable license and corresponding-source offer, while
RCL-owned branding, service terms, and third-party assets keep their own clear
license records.

The secure native-client API foundation remains disabled and is not a
production contract until its migrations, build registry, security review, and
controlled deployment are approved by RCL.

## System boundaries

```text
┌──────────────────────────────── RCL client ────────────────────────────────┐
│                                                                            │
│  Mouse-first menus and overlays                                            │
│       │                                                                    │
│       ├── Client session service ── OS browser + OS credential store       │
│       ├── Queue service ─────────── background poll/backoff state machine  │
│       ├── Server directory ───────── HTTPS list + cached UDP fallback      │
│       └── Game handoff ───────────── orderly disconnect/connect            │
│                              │                                             │
│                    Armagetron game engine                                  │
│                    simulation · rendering · UDP protocol                   │
└──────────────────────────────┬─────────────────────────────────────────────┘
                               │ HTTPS
                  ┌────────────▼────────────┐
                  │ RCL client API          │
                  │ enrollment · tokens    │
                  │ self-only queue state  │
                  │ release/build policy   │
                  └────────────┬────────────┘
                               │ server-derived identity
                  ┌────────────▼────────────┐
                  │ RCL queue + game fleet  │
                  └─────────────────────────┘
```

The queue service belongs to the process-level client session, not to a menu or
game connection. Connecting to a public server therefore cannot stop queue
polling.

## Secure identity

The native client follows the ranked-client API contract documented in the RCL
dashboard. Its invariants are:

- Sign-in and approval happen in the system browser. Supabase cookies never
  enter the game process.
- The browser creates a short-lived, one-use enrollment code for an approved
  public `clientId` and `buildId`.
- The client exchanges that code over HTTPS for a short-lived access token and
  a rotating refresh token bound to one server-side installation.
- The access token authorizes only the authenticated player's queue reads,
  joins, leaves, and optional telemetry writes.
- RCL derives the player identity from the installation. Queue mutations never
  accept a player name, profile ID, Discord ID, or another player's entry ID.
- The refresh token lives in macOS Keychain, Windows Credential Manager, or
  Linux Secret Service. Tokens never appear in cvars, normal configuration,
  console output, crash reports, or telemetry.
- `QUEUE_API_KEY`, service-role credentials, browser cookies, and Armagetron MD5
  responses are never distributed in the client.
- Client, build, installation, and session kill switches remain server-side.

The existing `/armaauth/0.1` flow remains limited to authentication with an
Armagetron game server. It is not a native-client API credential.

## Queue while playing

The queue lifecycle is independent of `nCLIENT` game connection state:

```text
Idle
  └─ Join lane ─▶ Searching
                    ├─ Play while waiting ─▶ Connected to public server
                    │                          (queue keeps polling)
                    ├─ Leave lane ──────────▶ Idle
                    └─ Match pop ───────────▶ Match found
                                               ├─ notify in game + OS
                                               ├─ reconcile queue state
                                               ├─ disconnect cleanly
                                               └─ connect assigned server
```

Polling uses `GET /api/client/v1/queue/me` every 3-5 seconds only while queued
or while a pop is active. Transport failures use bounded exponential backoff.
A `401` may refresh once; a mutation with an ambiguous result is never blindly
retried. The client first reconciles with `queue/me`.

"Play while waiting" chooses from the public server directory using explicit
filters and player preferences. It must not join a ranked/pickup target before a
pop. A pop is presented in the in-game menu layer and as an optional OS
notification. Auto-handoff can be enabled, but the default beta behavior should
show a short, unmistakable countdown so the player understands why the current
connection is ending.

## UI direction

Mouse support is a property of the shared menu system, not a collection of
one-off queue screens. Every menu row supports hover, click, wheel scrolling,
right-click back, keyboard navigation, and controller navigation.

The next menu layer should add:

- A persistent RCL identity and queue-status header.
- Large primary actions: **Queue**, **Play online**, **Training**, and
  **Settings**.
- Queue cards that show mode, skill scope, region preference, player count, and
  eligibility returned by the server.
- A live server browser sourced from RCL's HTTPS directory, with UDP discovery
  retained as a compatibility fallback.
- Searchable settings grouped by outcome (controls, camera, display, audio,
  network, accessibility), while retaining an advanced console/config view.
- In-game queue and match-pop overlays that do not capture gameplay input until
  the player deliberately opens or focuses them.

## Reliability and security work

Before a ranked release, the client and backend must have:

- Reproducible per-platform builds, a source commit/build manifest, package
  digest, and RCL release signature.
- HTTPS certificate and hostname verification, response-size/time limits, JSON
  type/length/depth limits, serialized refresh, and secret-redacted errors.
- Strict resource-download scheme, host, size, path, and archive limits so maps
  and textures cannot escape the resource directory or exhaust the client.
- Fuzz/property tests for network packets, resource paths, client API JSON, and
  menu/config parsing, plus sanitizer builds for the C++ client.
- Crash recovery that preserves safe settings but never persists access tokens
  or enrollment codes in logs.
- Competitive telemetry in shadow mode only, correlated with authoritative
  server state. Client telemetry is evidence, not remote attestation and not an
  automatic-ban oracle.
- Stored-result idempotency for queue mutations on the backend. Until that is
  available, the client reconciles every ambiguous join/leave result before
  offering a retry.

## Backend gates before native queue beta

The client must remain disabled until RCL has closed these server-side gates:

- Complete the authentication logging review and credential-rotation playbook
  before enabling native queue sessions.
- Add stored-result idempotency and active-pop protection to queue mutations so
  a lost response cannot reset queue age or create a second waiting entry.
- Make the supported mode list consistent. The current dashboard can display a
  `2s` lane while pickup-pop filtering covers only `sumo`, `tst`, and `fort`, so
  a non-triggering 2s player may not observe the active pop through normal state
  polling.
- Make allocation health-aware and cache the HTTPS server directory. Static
  exclusions can otherwise select an offline/full target, while probing every
  fleet server per client request would multiply UDP load as adoption grows.
- Apply and review the native-client migrations, register an RCL-owned public
  `clientId`, add reviewed build records, implement the browser enrollment UI,
  and exercise client/build/installation kill switches before enablement.
- Keep the unauthenticated legacy TCP queue service out of the distributed
  client. The native client uses only the scoped HTTPS API.

## Delivery plan

### M0 — compatible client foundation

- Build from the clean RCL fork, not the dirty production checkout.
- Make the common menu system usable with a mouse.
- Keep Linux, macOS, and Windows client smoke builds green.
- Produce complete packages with runtime libraries and data assets.

Exit: a player can install, launch, configure, browse, connect, play, and leave
using the mouse without changing the current network protocol.

### M1 — secure RCL session and queue

- Complete and review the disabled RCL native-client API.
- Add browser enrollment, HTTPS exchange/refresh/revoke, and per-OS credential
  storage off the render thread.
- Add self-only queue state, join, leave, eligibility, and error UI.

Exit: a beta installation can be revoked independently and can queue without
shipping a shared secret or copying a browser session.

### M2 — play while waiting

- Keep queue state alive across menu and game connections.
- Add public-server selection and a visible queue overlay.
- Add pop notification, reconciliation, orderly disconnect, and target connect.

Exit: a queued player can play on a public server until the assigned RCL match
is ready, then reach the target server without copying an address.

### M3 — modern configuration and shell

- Replace nested legacy setup screens with a consistent, searchable settings
  model and modern visual layout.
- Add controller parity, accessibility scaling, safe defaults, import, reset,
  and diagnostics.
- Harden resource delivery and automatic update verification.

Exit: ordinary play never requires editing a config file; expert controls remain
available and reversible.

### M4 — ranked release

- Enable only reviewed client/build records.
- Sign and publish reproducible packages and a verified update manifest.
- Run telemetry in shadow mode, correlate with server evidence, exercise kill
  switches, and complete rollback drills.

Exit: RCL can release, observe, revoke, and roll back the ranked client without
changing player accounts or fleet protocol.

### M5 — engine modernization

- With explicit approval, evaluate the current upstream Armagetron line and
  migrate rendering/window/input dependencies without changing game feel.
- Keep a recorded compatibility suite for physics, inputs, cameras, maps,
  protocol, and representative RCL matches.

Exit: supported platform APIs and dependencies replace the compatibility stack
while the game remains recognizably and measurably Armagetron Advanced.

## Decisions required before M1/M5

1. RCL approval to land, migrate, audit, and later enable the disabled native
   client API branch.
2. The public product name and `clientId` (`ilonium-ranked` is the development
   placeholder in the current backend work).
3. Explicit approval before merging or porting upstream `trunk`/SDL2 work.
