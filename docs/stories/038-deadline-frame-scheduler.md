# 038 - Deadline-based frame scheduler

Counterpart of [ReHLDS MT-101](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-101-deadline-frame-scheduler.md) and
[ReHLDS MT-103](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-103-timer-precision-fixes.md).

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
- [ ] Absolute deadlines on `CLOCK_MONOTONIC`, `clock_nanosleep(TIMER_ABSTIME)`
      on Linux, high resolution waitable timer on Windows, timer slack 1 ns.
- [ ] Late frames: catch up without bursts, re-anchor after a long stall
      (map change).
- [ ] Optional busy-wait before the deadline (`sys_spin_us`).
- [ ] Keep `sv_hibernate_when_empty` working (no players: long sleeps).
- [ ] Interaction with `sv_fps` documented.

## Acceptance criteria
- `sys_ticrate 1000`: 1000.0 fps measured by 035, p99 tick interval within
  a few percent of 1 ms on an idle and a 19 bot server.
- No change in the bytes sent for the same inputs (037).

## Dependencies
035.
