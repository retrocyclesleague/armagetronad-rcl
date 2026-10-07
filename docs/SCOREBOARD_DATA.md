# Scoreboard data: what a server tells the board

The RCL client's scoreboard (`docs/CLIENT_ARCHITECTURE.md`) shows what every
client is told anyway: names, colours, teams, scores, pings, who is alive.
Everything else on it comes from the server through the commands below. They
are for whoever writes a server's configuration or the script that runs its
game mode.

## The interface

Console commands, as in a config file, on the server's input or from a script.
On a client that is connected to a server they do nothing: its board is that
server's to write.

| Command | What it does |
| --- | --- |
| `RCL_BOARD_TITLE <text>` | What is played here, as the board's headline. The server's name moves to the line under it. |
| `RCL_BOARD_INFO <fact>, <fact>, ...` | Facts about the match, each shown on a small plate under the headline. |
| `RCL_BOARD_NOTE <text>` | One line under the list of players. |
| `RCL_BOARD_COLUMNS <heading>, <heading>, ...` | Columns of the server's own, between the score and the ping. |
| `RCL_BOARD_PLAYER <player> <value>, <value>, ...` | One player's values for those columns, in the same order. |
| `RCL_BOARD_CLEAR` | Forgets every player's values. |

A setting given no text is empty again and its part of the board is gone.

```
RCL_BOARD_TITLE Fortress
RCL_BOARD_INFO round 3 of 10, first to 100, ranked
RCL_BOARD_COLUMNS kills, deaths, zone
RCL_BOARD_PLAYER syn 9, 0, 1:15
RCL_BOARD_PLAYER Linux 3, 1, 0:42
```

Rules:

- `<player>` is one word. A player whose log name it is, exactly, is taken
  first: that is the name a script reads in the ladder log, and the one to
  use. Failing that it is looked up the way `KICK` looks a player up, by
  screen name (case and decoration ignored, a unique part of it is enough).
  A name that fits nobody, or more than one player, changes nothing.
- A player's values stay until they are set again, until `RCL_BOARD_CLEAR`, or
  until the player leaves. Nothing is reset at the end of a round or a match:
  the script decides.
- Values and headings are text. Colour codes (`0xRRGGBB`) work in all of it.
  A value cannot contain `,` `;` or `=`.
- Limits: 220 characters for each of the four settings, 24 for one value. The
  board shows the first five columns; two fit the panel as it is, each one
  after that makes it 62 pixels wider. All players' values together have room
  for about 1700 characters, which is 90 players with three short values
  each; past that the server says so on its console and leaves players out.
- Any admin who may change server settings may use these. `DELAY_COMMAND`
  works with them.

## How it travels

As a message of its own kind (descriptor 270, "RCL board line"): a line's
number and its text. The four settings are lines 0 to 3; lines 4 to 11 are
written by the game from `RCL_BOARD_PLAYER`, each
`<network ID>=<value>,<value>;...`. The server sends a line to everyone when
it has changed, and every line that is not empty to a client that has just
come in. Everything a script says in one go is collected and sent once, and a
change to one player's values resends the line that holds them, not the board.

A client that does not know the message drops it without a word: that is
what this game does with a kind of message it has never heard of. It is why
these are not server settings, which would travel for free: a client told
about a setting it does not know prints "YOU PROBABLY SHOULD UPGRADE" on its
console.

A client forgets what a server told it when it leaves, so nothing is carried
to the next server.

In a local game the same commands work on the console and show on the board
at once, which is the quick way to try a layout.

## Where the code is

`src/engine/eRclBoard.{h,cpp}`: the settings, the two commands and the reading
back. The pair uses nothing of the client's interface, so a server built from
another line of the source takes it as it is: the two files, their line in
`src/Makefile.am`, the six help texts in `language/english_base.txt`, and one
call in `ePlayerNetID`'s destructor (`eRclBoard::Forget( this )`). The drawing is
`src/engine/eRclScoreboard.cpp`, client only.

## Not there yet

Values for teams, a column that replaces the built-in score, sorting by a
column of the server's, and anything the server could say about a player
that is not text in a cell (a rank badge, a ready mark).
