# Dedicated server benchmarks

Measurements behind the dedicated server stories (034-050), made with
`scripts/server/bench/bench.sh` (story 036) on the server tree of
`scripts/server/setup.sh` (story 034): Counter-Strike with ReGameDLL_CS and
Metamod-FWGS, `de_dust2`, `sys_ticrate 1000`, the frame profiler (035) over
30 or 60 s, the CPU of the process over the same window.

Host: AMD64 desktop, 32 CPUs, `powersave` governor, nothing pinned. The load
clients and the flood run on the same host as the server.

`FEATURES=0` starts the server with every option of the stories off, the
behaviour of the upstream engine; the default `run.sh` turns them all on.

## 32 load clients (2026-10-15)

`CLIENTS=32`, no bots: 32 playing load clients (`loadclient.py --play`,
protocol 49), `cl_updaterate 100`, 100 commands per second, random send
phases. 30 s after 20 s of warm-up.

| Options | fps | tick p95 | tick p99 | frame p99 | `send_messages` p95 | snapshots/s per client | input latency | CPU |
|---|---|---|---|---|---|---|---|---|
| none (`FEATURES=0`) | 812.9 | 2176 µs | 2487 µs | 2486 µs | 1968 µs | 94.6 | 6.44 ms | 57 % |
| `sys_scheduler 1` only | 753.7 | 3613 µs | 4212 µs | | 3491 µs | 95.3 | 7.34 ms | 48 % |
| `sv_snapshot_spread 1` only | 999.8 | 1002 µs | 1007 µs | | 444 µs | 100.0 | 5.88 ms | 92 % |
| all (`run.sh`, `sys_spin_us 200`) | 1000.0 | 1000.1 µs | 1000.2 µs | 749 µs | 456 µs | 100.0 | 6.48 ms | 66 % |

- Without spreading, every client becomes due in the same frame and stays
  there: one frame in ten builds all 32 snapshots (`send_messages` p50 1 µs,
  p95 2 ms), the server misses 19 % of its ticks and the clients get 94.6
  snapshots per second instead of 100.
- The scheduler alone makes it worse: exact ticks keep the snapshots
  aligned. Spreading alone fixes the tick, but the original loop then spins
  (92 % of a core). Together: every tick on time, exact update rate.
- Per frame with all the options: gather 239 µs (game library:
  `AddToFullPack`, `UpdateClientData`), encode 56 µs, transmit 47 µs, reading
  the 3200 commands per second 119 µs; the frame takes half of the tick.

## 19 bots (2026-10-15)

`BOTS=19`, no client, 60 s. Bots receive no snapshots: this is the scheduler
alone.

| Options | fps | tick p95 | tick p99 | rejected frames | CPU |
|---|---|---|---|---|---|
| `sys_scheduler 0` | 977.2 | 1047.1 µs | 1400.3 µs | 2127 per frame | 22.4 % |
| `sys_scheduler 1` | 993.8 | 1003.3 µs | 1153.1 µs | 0 | 9.0 % |
| `sys_scheduler 1`, `sys_spin_us 200` | 991.7 | 1000.2 µs | 1301.3 µs | 0 | 28.4 % |

The ticks still missed are frames longer than a tick: the bots' AI
(`physics` p99 about 1.1 ms, max 19 ms).

## Floods, 19 bots (2026-10-15)

50 000 packets per second from 4096 addresses (`flood.py`), all the options
of 038-044 on, no network thread yet (story 040).

| Flood | fps | tick p99 | frame avg | `read_packets` avg / p99 / max | CPU |
|---|---|---|---|---|---|
| none | 990.7 | 1043 µs | 86 µs | 1.5 / 2.5 / 27 µs | 29.8 % |
| A2S_INFO queries | 983.8 | 1506 µs | 197 µs | 108 / 671 / 7272 µs | 40.9 % |
| junk game packets | 992.6 | 1096 µs | 141 µs | 60 / 185 / 341 µs | 35.7 % |

The engine's rate limiter is already O(1) (a hashed token bucket per address),
so a flood doesn't take the server down as it did with ReHLDS' stock engine;
it costs the main thread 60 to 110 µs per frame on average, with stalls of
several milliseconds under the query flood.
