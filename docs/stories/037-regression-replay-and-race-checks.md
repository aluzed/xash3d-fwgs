# 037 - Byte-exact replay and race checks

Counterpart of [ReHLDS MT-004](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-004-regression-replay-and-tsan.md).

## Context
Moving work to threads must not change a single byte the server sends. The
ReHLDS fork records a session's inputs (clock, packets, console) and replays
them as fast as possible, comparing every packet sent. Xash3D builds with
`-fsanitize=thread` (the job system of 029 was checked that way).

## Goal
`-netrecord <file>`, `-netreplay <file>`, `-netsends <file>` on the dedicated
server, and a ThreadSanitizer build that runs the load scenarios.

## Tasks
- [ ] Record and replay hooks around `NET_GetPacket`, `Platform_DoubleTime` and
      console input; deterministic seed for the server.
- [ ] Compare script (reused from ReHLDS).
- [ ] TSan dedicated build; scenario runs with no report.

## Acceptance criteria
- A session without bots replays byte for byte; a one-byte change is caught.
- No TSan report with every threaded option on under load.

## Dependencies
035, 036.
