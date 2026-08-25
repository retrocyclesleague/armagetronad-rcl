# RCL client product architecture

## Product promise

The RCL client is a protocol-compatible Armagetron Advanced player build with
first-class RCL identity, public-lobby discovery, and pickup queueing.

The first-load path is deliberately short:

1. Choose the normal local player settings.
2. Sign in once with an RCL game username and game password, or continue as a
   guest.
3. From the main menu choose **Play Now** or **Queue Now**, then Fort, Sumobar,
   or TST.
4. The client selects a healthy, non-full regional lobby from the dynamic RCL
   fleet and connects to it.
5. Queue Now sends `/add` after the server has verified the player's Global ID.

The gameplay simulation, maps, camera behaviour, and network protocol remain
compatible with the current sty+ct+ap fleet.

## Shipped v1 boundaries

```text
RCL client
  ├─ first-load RCL sign-in ── /armaauth/0.1 methods/params/check
  ├─ public profile summary ── /armaauth/0.1 query=profile
  ├─ Play Now resolver ─────── public live-server directory + UDP probe
  └─ Queue Now ─────────────── connect, authenticate, then send /add
                                      │
                                      ▼
RCL game lobby ── verified COMMAND/PLAYER_LOGIN ladderlog events
                                      │
                                      ▼
mode-scoped bridge ── authenticated server command ── dashboard pickup queue
```

The game client never contains a dashboard service key, Supabase cookie, bot
credential, or queue ingest secret. Queue authority stays server-side: the
bridge accepts the identity the game server authenticated, resolves linked RCL
or legacy identities, and calls the existing queue service with its own
mode-scoped credential.

## Identity and authentication

### First load

After language and local-player setup, the client offers RCL sign-in before the
main menu. The prompt uses the existing ArmaAuth password-scramble code and the
production authority at `retrocyclesleague.com/armaauth/0.1`:

1. Fetch supported methods and method parameters.
2. Derive the salted base credential with the existing Krawall routine.
3. Validate it using a random challenge through `query=check`.
4. On success, set `USER_1` to `username@rcl` and cache public profile data.

Plaintext is cleared after derivation. When the player elects to save the
credential, the existing Armagetron password store persists the derived base
credential, not plaintext. That value can answer RCL challenges and must still
be treated as a secret. A player may choose memory-only/no storage, at the cost
of being prompted again.

### Returning launch

For `@rcl` users with a saved credential, startup silently validates it and
loads the profile summary before showing the main menu. This replaces a visible
RCL **Auto Login** setting. The client still enables the internal per-server
automatic response because every newly joined game server independently issues
its own authentication challenge.

The generic Player Setup **Auto Login** control remains for community
authorities and older workflows; it is not part of the RCL Account UI.

### Legacy identities

Linked identities such as `name@forums` continue to use the stock server
authentication handshake. They do not perform an RCL boot preflight. Once the
server authenticates the identity, the bridge maps it to the linked RCL profile
before accepting `/add`. Unlinked identities receive a link-account error;
guests cannot queue.

### Public profile

`query=profile&user=<name>` returns a bounded plain-text summary containing the
canonical identity, public username, current rank, Elo, and match count. It
contains no session or private account data. The client shows this summary in
the RCL Account main-menu row. The game server remains authoritative for the
welcome message and queue identity.

## Play Now

Play Now exposes three product choices: Fort, Sumobar, and TST. Each maps to a
dynamic fleet rather than a fixed host. The resolver obtains the current RCL
server list, filters by mode and capacity, probes candidates, and prefers a
healthy non-full regional lobby. The chosen server supplies its normal managed
map and settings; the client does not embed competitive configs.

If the directory is temporarily unavailable, the resolver may use its bounded
cached candidates. Failure must return to the menu with an actionable message,
not connect to a full or unrelated server.

## Queue Now and `/add`

Queue Now uses the same lobby resolver, connects, waits for authentication, and
sends `/add`. This intentionally reuses the queue command older clients already
support.

The fleet bridge handles the race where `COMMAND /add` reaches the ladderlog
before `PLAYER_LOGIN`: it holds the request briefly, releases it when the
verified login arrives, and rejects it if authentication never completes. The
bridge then:

- resolves `name@rcl` or a linked legacy Global ID;
- checks the required linked Discord identity for pickup notifications;
- joins the appropriate mode lane through the dashboard queue API;
- returns queue count or eligibility errors to the server; and
- provides public rank/Elo for the authenticated welcome.

No player-supplied display name is trusted for queue mutations.

## UI direction

The main menu presents:

- **Play Now** — Fort, Sumobar, or TST;
- **Queue Now** — the same choices with authenticated `/add` after join;
- **RCL Account** — sign in, switch account, or show identity/rank/Elo/matches;
- the existing local play, Internet play, settings, and Player Setup paths.

Future queue polling, match-pop overlays, and automatic match handoff may use a
reviewed installation-scoped HTTPS session. Browser enrollment and OS credential
storage are future architecture, not requirements for the shipped server-bridge
v1 and not claims about current behaviour.

## Reliability and release constraints

- Stay on the `0.2.9+sty+ct+ap+rcl` protocol-compatible line.
- Never distribute server queue keys, dashboard cookies, or service-role keys.
- Do not log plaintext passwords, derived credentials, challenge hashes, or
  salts.
- Keep HTTP compatibility only for the established RCL ArmaAuth authority path;
  broader future client APIs require verified HTTPS.
- Keep Linux, macOS, Windows, and dedicated builds green.
- Publish tagged, reviewed packages; ranked fleet binaries require separate ops
  sign-off.
- A failed RCL startup validation falls back to guest mode and leaves manual
  retry available under RCL Account.

## Verification contract

A release candidate is not complete until a fresh-profile and returning-profile
test prove:

1. first-load sign-in appears before the main menu and cancel preserves guest
   play;
2. a valid RCL credential produces the public profile summary;
3. restart validates silently without another credential prompt;
4. Play Now selects a correct, non-full Fort, Sumobar, and TST lobby;
5. the selected server runs the managed map/settings for that mode;
6. Queue Now authenticates, emits `/add`, and the bridge joins the pickup queue;
7. a linked legacy Global ID can type `/add` and reaches the same canonical
   queue identity; and
8. `/remove` cleans up the test queue entry.
