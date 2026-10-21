# Dedicated server tuning

How to get a steady tick and protect it from packet floods with the options of
the dedicated server stories 034-050 (`docs/stories/`), brought over from the
multithreaded ReHLDS fork. Every option is off by default until it is
validated on a server with players (story 050); `scripts/server/run.sh` turns
them all on. The measurements behind each line are in
[`../docs/benchmarks/README.md`](../docs/benchmarks/README.md).

## Every option at once

```sh
scripts/server/setup.sh   # once: server tree, Metamod-FWGS, configs
scripts/server/run.sh     # build, install, start with every option on
FEATURES=0 scripts/server/run.sh   # the same server with every option off
```

or by hand:

```sh
./xash -game cstrike -sys_ticrate 1000 -netthread -netsend -sys_threads 2 \
	+sys_scheduler 1 +sv_snapshot_spread 1 +sv_log_async 1 \
	+sv_client_lookup_cache 1 +sv_a2s_cache 1 +sv_parallel_send 1 +map de_dust2
```

## Options

| Option | Story | Default | Recommended | Effect |
|---|---|---|---|---|
| `sys_scheduler` | 038 | 0 | 1 | Frames on a grid of deadlines: exact `sys_ticrate`, no spinning between frames |
| `sys_spin_us` | 038 | 0 | 0 | With the scheduler, busy-wait that long before each tick (cores dedicated to the server only) |
| `sv_snapshot_spread` | 039 | 0 | 1, with the scheduler | Each client's snapshots at a fixed phase: exact update rate, snapshots spread over the frames |
| `-netthread`, `-nonetthread` | 040 | off | on | Server sockets read on a thread |
| `-netthread_queue` | 040 | 1024 | 1024 | Packets the thread may queue for the main thread |
| `net_thread_filter` | 040 | 1 | 1 | The thread drops banned addresses, queries above the rate limits, game packets from unknown addresses |
| `sv_a2s_cache` | 042 | 0 | 1 | Server browser queries (A2S) answered by the network thread |
| `-netsend`, `-nonetsend` | 041 | off | on | Server packets sent from a thread, `sendmmsg` on Linux; the pause between the parts of a split packet leaves the main thread |
| `-netsend_queue` | 041 | 1024 | 1024 | Packets queued for the send thread |
| `sv_log_async` | 043 | 0 | 1 | Server log and `-log` console log written on a thread (next log file) |
| `sv_log_async_overflow` | 043 | `block` | `block` | Full log queue: wait for room, or drop the line (`drop`) |
| `sv_client_lookup_cache` | 044 | 0 | either | Client of a game packet found by address; no measurable gain at 32 players |
| `sv_parallel_send` | 045-048 | 0 | 1 | Snapshots gathered first, encoded on the workers, sent in client order; byte-identical |
| `-sys_threads N\|auto` | 047 | none | 2 | Encoding workers (at most 8) |
| `sys_threads_spin_us` | 047 | 0 | 0 | Workers spin between frames (cores dedicated to the server only) |
| `sv_profile*` | 035 | off | on demand | Per-phase frame profiler |
| `-netrecord`, `-netreplay`, `-netsends` | 037 | off | – | Byte-exact session replay, for developers |

`sys_ticrate` keeps its default (100): the 1000 used by the measurements costs
CPU and is the operator's choice. `-sys_ticrate` and the threads' switches are
read once, from the command line; the cvars can change at any time.

### Scheduler and spreading go together

With 32 players at 100 updates per second:

| Options | fps | tick p99 | updates per player | CPU |
|---|---|---|---|---|
| none | 812.9 | 2487 µs | 94.6 | 57 % |
| `sys_scheduler` only | 753.7 | 4212 µs | 95.3 | 48 % |
| `sv_snapshot_spread` only | 999.8 | 1007 µs | 100.0 | 92 % |
| both | 1000.0 | 1000.2 µs | 100.0 | 66 % |

Without spreading, all the snapshots of the server are built in one frame out
of ten; exact ticks alone keep them there. Spreading alone fixes the tick but
leaves the original loop spinning between frames.

### Bots and hibernation

Without players, Xash3D sleeps 50 ms per frame (`sv_hibernate_when_empty 1`;
bots don't count): bots then play at 20 Hz. A server with bots should set
`sv_hibernate_when_empty 0` (the `scripts/server` config does).

ReGameDLL_CS only registers its bots on a dedicated server with `bot_enable 1`
in `game_init.cfg`, and needs `BotProfile.db`, which the Steam data lacks
(cs16-client's `extras.pk3` has it).

How many bots: `modes/bots.cfg`, executed by the game modes that have bots
([game-modes.md](game-modes.md)).

## Counters

`net_thread_stats` (network thread, A2S answers), `net_send_stats` (send
thread), `sv_log_async_stats` (log writer), `sv_parallel_send_stats`
(encoding workers), `sv_profile <seconds>` (tick rate,
jitter, time of every phase; `sv_profile_csv <file>` for every frame).

## The machine

- **CPU frequency**: `powersave` and `schedutil` wake the core slowly between
  ticks; `cpupower frequency-set -g performance` gives steadier frames (the
  measurements ran with `powersave`).
- **One core per server**: the game loop is one thread; pin each server with
  `taskset -c N`, keep the network threads off the game loops' cores if you
  can, and avoid two servers on the two hardware threads of one core.
- **Socket buffers**: a larger receive buffer gives the network thread room
  under a flood: `sysctl -w net.core.rmem_default=4194304 net.core.rmem_max=4194304`.
- **Timer slack**: the scheduler sets its own to 1 ns on Linux.

## Snapshot encoding

With 32 players at 100 Hz the snapshot work is about 330 µs per frame:
280 µs in the game library (`AddToFullPack`, `UpdateClientData`), which stays
on the main thread, 33 to 47 µs of encoding and 19 µs of sending. The encoding
workers (`sv_parallel_send 1 -sys_threads 2`) save about 13 µs per frame
there; they matter with more players or entities per frame.
