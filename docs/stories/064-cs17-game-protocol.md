# 064 - A protocol of Counter-Strike 1.7's own (Counter-Strike)

## Context
The server browser lists every Counter-Strike server the masters know:
Xash3D servers (protocol 49) and GoldSrc servers (protocol 48), with a
filter between the two. Counter-Strike 1.7 is no longer the game those
servers run: its renderer, its smoke volume, the molotov, the thrown knife,
the scoreboard by damage, the game modes to come need this set's client
and server on both ends. On a GoldSrc or plain Xash3D server the player
lacks all of it; on a 1.7 server a plain client lacks the models, the
messages and the rules.

The network protocol (`PROTOCOL_VERSION` 49, `engine/common/protocol.h`)
is the engine's, shared with Half-Life 1.5 (`halflife-15`) and every Xash3D
game: changing it would cut this engine off from them all, Half-Life
included, for a difference that is the game's, not the engine's.

## Goal
Counter-Strike 1.7 has a game protocol of its own, a name and a number
carried on top of the engine's protocol 49. The browser lists only the
servers of the same game protocol; GoldSrc servers and other Xash3D
Counter-Strike servers are no longer shown. A client and a server of
different game protocols refuse each other at connection, with a message
that says why and which version is needed.

## Scope
In: the server's info answers and its connection check, the client's
connect request, the menus' server browser (both lists, the added
servers, the history), the console's `connect`.

Half-Life 1.5 takes the same mechanism, its own name (`hl15`).

Out: the engine's network protocol (stays 49), a master server of this
set's own (the Xash3D masters keep listing the servers; the client
filters).

## Status (2026-10-16)
Done. The game's server DLL sets `sv_gameprotocol` (`cs17-1` in
ReGameDLL_CS, `hl15-1` in Half-Life 1.5's), its client DLL
`cl_gameprotocol`, both at every start (not settings). The engine:
- server: `gp` in the info answer to the browsers, a third argument of the
  connection's challenge (older clients read two), the client's `gp` from
  the connect request checked: "This server needs game protocol cs17-1,
  your game has none";
- client: `gp` in its connect request, the server's checked on the
  challenge before anything is loaded: "Cannot join this server. It runs
  game protocol cs17-1, this game has cs17-0."; a GoldSrc server has none;
- a listen server is the same game: no check on loopback.
The browsers (both games) list only the servers of their own `gp` (in LAN
too), the protocol filter and the protocol choice of "Add server" are
gone. Decided: refused both ways, Half-Life 1.5 too; the Xash3D masters
kept.

Tested on the local server: info answer and challenge carry `cs17-1`; this
client joins; a client forced to `cs17-0` is refused before loading; a
raw Xash3D connect without `gp`, or with `hl15-1`, is refused by the
server. Left: the browser's list seen with a plain cstrike server beside
ours, Half-Life 1.5 played (single player, a LAN server), the Linux
dedicated server rebuilt with it.

## Design
- The game protocol: a name and a number, `cs17` and 1, set by the
  client and server DLLs (cvars the engine reads), bumped when a change makes an older client or server unable to
  play with the new one (a new message, a new weapon, a change of rules
  that the client predicts), not at every build.
- Server: its info answer (`A2S_INFO` and Xash3D's info string) carries
  `\gp\cs17-1`; a client whose connect request has another game protocol,
  or none, is refused: "This server runs Counter-Strike 1.7 (protocol 1)".
  GoldSrc clients (protocol 48) are refused too.
- Client: its connect request carries `gp`; it refuses a server without
  `gp` or with another one, before loading anything: "This server is not a
  Counter-Strike 1.7 server" or "needs Counter-Strike 1.7 protocol N".
- Browser: only servers announcing the same `gp` are listed; the protocol
  filter (Xash3D 49 / GoldSrc 48 / All) is removed, so is the protocol
  choice when adding a server by address.
- LAN: the same filter.

## Tasks
- [ ] The game protocol's name and number, where both DLLs read it.
- [ ] Server: `gp` in the info answers, the check at connection.
- [ ] Client: `gp` in the connect request, the check of the server's.
- [ ] Browser: the filter, the protocol filter removed, the greyed entries.
- [ ] Test: our server listed and joined; a GoldSrc-protocol server and a
      plain Xash3D cstrike server not listed and refused by `connect`; a
      plain client refused by our server.

## Acceptance criteria
- The browser shows only Counter-Strike 1.7 servers of the same protocol.
- Connecting to any other server, by the console too, is refused with a
  clear message, and the same the other way round.
- Half-Life 1.5 and other Xash3D games are not affected.

## Dependencies
None.

## Risks / notes
- The Xash3D masters filter by game directory: their answer still holds
  every cstrike server, the client sorts them; a master of this set's own
  would spare the queries (later, if the list grows).
- A number bumped too seldom lets an old client in to misbehave; too often
  splits the players: bumped with the changes that the other side needs.
