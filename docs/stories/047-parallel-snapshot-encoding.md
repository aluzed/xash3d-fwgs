# 047 - Parallel snapshot encoding on the job system

Counterpart of [ReHLDS MT-403](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-403-parallel-encode-and-transmit.md).

## Context
After 045 and 046, encoding a client's snapshot reads only gathered data. The
engine already has a worker pool (`engine/common/jobs.c`, story 029,
`-jobthreads`).

## Goal
`sv_parallel_send 1`: the frame's snapshots are encoded on the workers, the
main thread transmits them in client order. Counters in
`sv_parallel_send_stats`. Decide between the 029 job pool and dedicated
spinning workers (`sys_threads`, `sys_threads_spin_us` in ReHLDS) with numbers.

## Acceptance criteria
- Byte-identical replays with the option on and off.
- Encode time of the frame measured on the 32 client and many entities scenario.

## Progress (2026-10-15)
- [x] `engine/server/sv_workers.c`: `-sys_threads N|auto` (at most 8, none by
      default), `sys_threads_spin_us`, `sv_parallel_send_stats`.
      `SV_ParallelFor` hands the gathered snapshots out to the workers and to
      the main thread under the pool's lock (a worker takes an item only for
      the job it woke for), and returns when they are all encoded; the main
      thread then sends them in client order. Dedicated workers rather than
      the job system of 029: its `done` callbacks run on the next host frame.
- [x] Byte-identical: a session recorded with `sv_parallel_send 0` replays
      byte for byte with `sv_parallel_send 1` and 2 or 4 workers.
- [x] 32 load clients at 100 Hz, every other option on:

  | | encode per frame avg / p99 / max | send_messages avg | snapshots by the workers | CPU |
  |---|---|---|---|---|
  | `sv_parallel_send 0` | 46.5 / 77.6 / 138 µs | 360 µs | 0 | 80 % |
  | `sv_parallel_send 1`, 2 workers | 33.0 / 53.6 / 2134 µs | 326 µs | 2 of 3.2 per frame | 80 % |
  | the same, `sys_threads_spin_us 200` | 32.8 / 52.6 / 571 µs | 331 µs | 2 per frame | 120 % |

  As in ReHLDS the gain is small at 32 players: with spread snapshots a
  frame encodes about 3 of them; the frame is the game library's gather
  (277 µs). The sleeping workers' wake-up shows in the maximum.

## Dependencies
046.
