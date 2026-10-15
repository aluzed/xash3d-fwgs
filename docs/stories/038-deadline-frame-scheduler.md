# 038 - Deadline-based frame scheduler

Counterpart of [ReHLDS MT-101](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-101-deadline-frame-scheduler.md) and
[ReHLDS MT-103](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-103-timer-precision-fixes.md).

## Context
The dedicated server loop (`Host_Main` → `COM_Frame` → `Host_Frame`) spins and
lets `Host_Autosleep` (`engine/common/host.c`) decide whether to run a frame:
the target is `1 / (sys_ticrate + 1)`, and in between it sleeps
`sleeptime * 100 µs` slices inside a time window derived from the last
frame's `host.pureframetime`. The tick drifts with the sleep granularity and
the frame cost, and `sys_ticrate` defaults to 100. `sv_fps` can decouple
the simulation rate, `sv_hibernate_when_empty` sleeps 50 ms when no player
is connected (bots excluded).

## Goal
`sys_scheduler 1`: frames on a fixed grid of deadlines, one every
`1/sys_ticrate`, so the server runs exactly `sys_ticrate` frames per second
with low jitter. Same cvars as ReHLDS: `sys_scheduler`, `sys_spin_us`,
`sys_scheduler_slice` where it applies.

## Tasks
- [x] Absolute deadlines on `CLOCK_MONOTONIC`, `clock_nanosleep(TIMER_ABSTIME)`
      on Linux, high resolution waitable timer on Windows, timer slack 1 ns
      (`engine/common/host_scheduler.c`). The Windows path is not built yet.
- [x] Late frames: catch up without bursts, re-anchor after a long stall
      (more than 2 ticks late).
- [x] Optional busy-wait before the deadline (`sys_spin_us`).
- [x] Keep `sv_hibernate_when_empty` working: a hibernating server goes back
      to `Host_Autosleep`'s long sleeps.
- [x] `sv_fps` is untouched: it splits the simulation inside a host frame,
      the scheduler paces the host frames.

Xash3D owns its main loop (`Host_Main`), so there is no launcher time slice
as in ReHLDS: the loop waits for the next deadline before every
`COM_Frame`, and `Host_FilterTime` accepts the paced frame without
`Host_Autosleep`. `sys_scheduler_slice` has no counterpart.

## Progress (2026-10-15)
034 server, 19 bots, `de_dust2`, `sys_ticrate 1000`, 60 s each, one after the
other; CPU of the process over the capture:

| | fps | tick p95 | tick p99 | tick max | rejected frames | CPU |
|---|---|---|---|---|---|---|
| `sys_scheduler 0` | 977.2 | 1047.1 µs | 1400.3 µs | 21.9 ms | 2127 per frame | 22.4 % of a core |
| `sys_scheduler 1` | 993.8 | 1003.3 µs | 1153.1 µs | 18.7 ms | 0 | 9.0 % |
| `sys_scheduler 1`, `sys_spin_us 200` | 991.7 | 1000.2 µs | 1301.3 µs | 19.5 ms | 0 | 28.4 % |

The ticks the server still misses are frames longer than a tick: the p99
and max frames are `physics`, the bots' AI in the game library.

## Acceptance criteria
- `sys_ticrate 1000`: 1000.0 fps measured by 035, p99 tick interval within
  a few percent of 1 ms on an idle and a 19 bot server.
- No change in the bytes sent for the same inputs (037).

## Dependencies
035.
