# 037 - Byte-exact replay and race checks

Counterpart of [ReHLDS MT-004](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-004-regression-replay-and-tsan.md).

## Context
Moving work to threads must not change a single byte the server sends. The
ReHLDS fork records a session's inputs (clock, packets, console) and replays
them as fast as possible, comparing every packet sent. Xash3D builds with
`-fsanitize=thread` (the job system of 029 was checked that way).

## Goal
`-netrecord <file>`, `-netreplay <file>`, `-netsends <file>` on the dedicated
server, and a ThreadSanitizer build that runs the load scenarios.

## Tasks
- [x] Record and replay (`engine/common/net_replay.c`, port of the ReHLDS
      replay): the realtime of each accepted frame (`Host_FilterTime`), the
      packets `NET_GetPacket( NS_SERVER )` returns and its empty answers,
      console input (`Host_GetCommands`), the random seed (`COM_SetRandomSeed`)
      and the clock the game library and `pmove` read (`Sys_DoubleTime`,
      `Sys_FloatTime`, `pfnTime`). Sends are written in the ReHLDS format.
      A per-frame fingerprint of the entities' positions locates a divergence.
- [x] Compare script (`scripts/server/bench/replay_compare.py`, from ReHLDS),
      `bench.sh` with `RECORD=1`, `replay.sh <run>`.
- [x] TSan dedicated build; scenario runs with no report.

## Progress (2026-10-15)
- 8 load clients, 10 s: 15 290 packets to 8 destinations replayed byte for byte
  in 2.6 s. With one byte added to every datagram (`svc_nop` before
  `svc_time`), the compare finds the 8 destinations different.
- **Sessions with bots replay too** (4 bots and 4 playing clients, 7705
  packets identical): ReGameDLL_CS's bots draw from the engine's random
  generator and read the engine's clock, both recorded. On ReHLDS they never
  replayed.
- TSan: `CFLAGS=-fsanitize=thread` dedicated build in `build-tsan`
  (`WAFLOCK=.lock-waf_tsan`), run under `setarch x86_64 -R` (the kernel's
  address randomisation breaks TSan) with `LD_LIBRARY_PATH` set (the
  executable's `$ORIGIN` runpath isn't used then), and without Metamod:
  Metamod loads the game library with `RTLD_DEEPBIND`, which sanitizers
  refuse. 8 clients and 4 bots with every option on (`sv_log_async 1`,
  `-log`): no report.

```
cd ~/xash-dev && T=$PWD/server-tsan
METAMOD=0 XASH_SERVER_ROOT=$T NO_BUILD=1 XASH_WRAPPER="setarch x86_64 -R env LD_LIBRARY_PATH=$T" \
	TSAN_OPTIONS="log_path=$PWD/bench/tsan/report" CLIENTS=8 BOTS=4 \
	/var/www/xash3d-fwgs/scripts/server/bench/bench.sh tsan
```

## Acceptance criteria
- A session without bots replays byte for byte; a one-byte change is caught.
- No TSan report with every threaded option on under load.

## Dependencies
035, 036.
