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
4. The client probes the RCL regional entry points for that mode and connects
   to a reachable, non-full lobby in the nearest region.
5. Queue Now sends `/add` once the client has answered the server's password
   request; the fleet bridge only honours it after the server has verified the
   player's Global ID.

The gameplay simulation, maps, camera behaviour, and network protocol remain
compatible with the current sty+ct+ap fleet.

## Shipped v1 boundaries

```text
RCL client
  ├─ first-load RCL sign-in ── /armaauth/0.1 methods/params/check
  ├─ public profile summary ── /armaauth/0.1 query=profile
  ├─ Play Now resolver ─────── fixed regional hosts, per-mode port, UDP probe
  └─ Queue Now ─────────────── connect, answer the login, then send /add
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

For `@rcl` users with a saved credential, startup validates it without a
prompt and loads the profile summary before showing the main menu. The requests
run under a "Signing in to RCL" frame that keeps drawing and reading input; the
whole exchange has a 5 second budget and Escape skips it. Only resolving the
authority's host name is still a blocking call. A timeout or transport failure
is reported as the service being unavailable and leaves the saved credential in
place; only an explicit refusal from the authority is reported as a rejected
sign-in. This replaces a visible
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
canonical identity, public username, TST tier/rank/Elo/match count, and total
public match count. It contains no session or private account data. The client
treats it as untrusted text: colour codes and control characters are stripped
from the username and tier, both are capped at 32 characters, and numbers
outside a sane range are discarded. The main
menu mirrors the RCL home hierarchy with a compact `NAME // TIER // ELO` row;
rank and match detail live in its two-line help panel. The game server remains
authoritative for the welcome message and queue identity.

## Play Now

Play Now exposes three product choices: Fort, Sumobar, and TST. The candidate
list is compiled in (`sg_rclPlayNowHosts` in `src/tron/gGame.cpp`): four
regional host names, with one fixed port per mode. There is no server-directory
fetch and no candidate cache; adding a region or moving a port needs a client
release. A directory served by the authority is future work.

The resolver probes every candidate over UDP for at most 3 seconds while it
keeps drawing a "finding a lobby" frame; Escape cancels back to the menu.
Candidates that did not answer, are full, or whose server name does not match
the mode are dropped. Of the rest, only lobbies within 60 ms of the best ping
compete, so a player is kept in their own region; among those the most
populated lobby wins and ping breaks ties. The chosen server supplies its
normal managed map and settings; the client does not embed competitive configs.

If nothing qualifies, the resolver returns to the menu with an actionable
message rather than connecting to a full or unrelated server.

## Queue Now and `/add`

Queue Now uses the same lobby resolver and connects. It sends `/add rcl-client`
one second after the client has sent a non-aborted answer to that server's
password request, whether the answer came from the stored credential or from
the prompt. If the prompt is cancelled, or the server has not asked within 45
seconds, nothing is sent and the console says so. The client cannot observe the
server's verdict, so the bridge remains the authority on whether the login
succeeded. The optional argument lets pickup chat credit the RCL
Game Client; it is presentation metadata, not security attestation. This still
reuses the `/add` command older clients support.

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

## Interface

Every menu, prompt and dialog is drawn by one presentation layer,
`src/ui/uRclTheme.*`. It follows the RCL component kit's inverse (dark)
treatment; draw code elsewhere carries no colours or sizes of its own.

| Token | Value | Use |
|-------|-------|-----|
| canvas | `#141617` | outer background; holds back the scene behind a menu |
| surface | `#202122` | the navigation rail, tables, the prompt bar |
| surfaceSelected | `#5B5D60` | the selected row |
| borderSubtle / borderStrong | `#3D3E3F` / `#646668` | separation / control outlines |
| textPrimary | `#FAFAFA` | labels, values in the selected row |
| textSecondary | `#B2B4B5` | values, help, hints |
| accent | `#EEFF41` | the primary action, the selection edge, focus, switched-on controls |
| onAccent | `#202122` | text on the accent |

`surfaceRaised` (`#3B3D3F`) and a `danger` colour are defined and not used yet.

- **Type.** Space Grotesk 400/500/600 (700 for the wordmark only). The engine
  has no font rasteriser, so `scripts/generate-rcl-ui-font.py` rasterises the
  font file the site serves into one atlas per weight and pixel size
  (`textures/ui/`). `rUiFont` picks a size it can draw texel for pixel;
  `rTextField` places glyphs by their advances, with tabular digits, and
  measures and wraps text by width. This applies to all text in the client,
  the HUD and console included. Lines padded with runs of spaces (console
  tables) keep their columns on the old cell grid. `RCL_UI_FONT 0` falls back
  to the fixed-width bitmap font.
- **Lowercase.** Authored English strings are lowercased once, when the
  language file is loaded (`tLocaleItem::Load`). Names, chat, typed text and
  anything a server sends are never transformed.
- **Layout.** Sizes are logical pixels: real pixels on a 1080-line display,
  scaled with the window height, never below 0.6 (smaller windows scroll).
  Four layouts: home (the main menu), page, wide (a table) and prompt (chat
  and console input along the bottom). The layout is compact: rows are 36
  apart (32 in a table), the navigation column is 300 wide and a page with
  controls 600, and all of it is set in one block of constants at the top of
  `uRclTheme.cpp`. The wordmark is on the first screen only; a page is named
  by its title. Besides the rows, a menu shows the selected row's help, and
  a page a quiet "esc back"; only a table lists keys, because it has keys of
  its own.
- **No "back" rows.** A menu makes itself a row to leave by. In the panels
  that row is not shown while the menu has any other row (`uMenu::HidesRow`):
  Escape and the right mouse button leave a menu. A menu with nothing else in
  it keeps the row, and rows a page names itself (cancel, quit, resume) stay.
- **Components.** Action row; one primary action per menu (filled accent);
  setting row with a separate control column: toggle, selector, slider, text
  field, key binding; table rows with measured, right-aligned number columns;
  dialog with wrapped, scrollable text; the selected row's help in the footer.
  A menu item says which control it has (`uMenuItem::Control()`); `uMenu`
  draws the row and routes the pointer (hover, click, selector arrows, slider
  press and drag).
- **Settings are not rewritten by opening a page.** Number rows no longer
  clamp their value when a menu is built; a value outside the range, or one
  no selector choice stands for, is shown as it is until the player changes
  it.

The tree is short on purpose, and nothing the client ever had a menu for is
gone: what is not on the short pages is one level down, under Advanced, on
the pages it always had.

- Main menu: play now (primary), queue now, servers, practice, settings,
  quit, with the client version and sign-in status at the foot of the rail.
  Servers opens the server list; practice starts a local game.
- Settings: account, player, controls, display, audio, advanced. Player,
  controls and display are one page each of what a player sets, for the first
  player (`sg_RclPlayerPage`, `sg_RclControlsPage`, `sg_RclDisplayPage` in
  `gMenus.cpp`): name, colour, field of view and camera; the three lists of
  keys and the instant chat texts; window, resolution, vsync, anti-aliasing,
  the arena's look, reflections, the frame counter.
- Advanced: practice game (local game settings), network (LAN, connecting by
  address, bookmarks, mates, other server lists, network setup), all player
  options (players 2 to 4, split screen, team, spectator, identity), all
  display options (screen mode, preferences and HUD, detail, performance),
  interface (language, menu wrap, text output, moviepack, first setup),
  extras, config files, about.
- In-game menu: resume (primary), team, vote and player police online,
  change game locally, settings, authentication where a server offers it,
  and leave match or disconnect last.

Escape goes back; on the main menu it first moves to quit.

The scoreboard (`eRclScoreboard`, engine layer; the scores key or the end of
a round shows it) is a panel drawn with the same theme, 600 logical pixels
wide so a portrait capture of the window keeps all of it. Top to bottom: the
server's name as the server gives it (or "Local game"), the round's clock, the
map's name taken from its resource path, how many play and how many watch;
then each team as a band in its colour with its score large and how many of
it are alive; then its players by score, each with a square in their colour
(filled while alive, an outline once out), name, score and ping, "typing"
while they chat, and the local player's row outlined in the accent; then who
watches. Where nobody shares a team the bands are left out and players are
numbered. Rows close up and then are left out ("and 3 more") before the panel
outgrows the window. `RCL_SCOREBOARD 0` brings the text table back.

Of its own accord a server sends names, colours, teams, scores, pings and who
is alive: no kills or deaths, no score or round limit, no round number and no
mode. Whatever else is to be on the board, the server says (`eRclBoard`,
`docs/SCOREBOARD_DATA.md`): a title that becomes the headline, facts shown on
plates under it, a note under the list, and columns of its own between score
and ping with a value in each for every player. A server or its mode's script
sets them with `RCL_BOARD_*` console commands; they travel as one kind of
message that clients without it drop silently. With more than two such
columns the panel grows wider than a portrait capture keeps.

Not yet in this system: the HUD's layout (only its font), connection and
loading screens, the first-run setup, a dedicated camera page, and distinct
empty/loading/error states in the server browser.

Future queue polling, match-pop overlays, and automatic match handoff may use a
reviewed installation-scoped HTTPS session. Browser enrollment and OS credential
storage are future architecture, not requirements for the shipped server-bridge
v1 and not claims about current behaviour.

## Round lag log

The client measures its connection during every online round and can say what
it found (`gRclLagLog` in `gGame.cpp`; `RCL_LAG_LOG 0` turns it off). Only the
time a local player is alive counts.

- Measured: average ping and the longest wait for an acknowledgement; how
  often and for how long the server was silent (0.3 s or more is a freeze);
  how much older cycles' syncs arrived than their best in the round; messages
  sent and sent again; other players whose reported ping moved; and frames of
  the client's own over 0.1 s, which are kept apart from the rest.
- `var/rcl-lag.log`: one tab-separated line per round, with a header. It
  starts over at half a megabyte; the one before is kept as
  `rcl-lag.old.log`. Rounds left in the middle are logged as far as they got.
- A round that lagged, or in which the machine stalled, gets a console line
  when it ends.
- `/lag` in chat says the line to the server: the round being played if it has
  lagged so far, else the round that just ended if that one did. Without lag
  in either, the line is shown to the player only. A stall of the player's
  own machine is always part of the line.

Nothing in it leaves the machine except the chat line the player asks for.

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
