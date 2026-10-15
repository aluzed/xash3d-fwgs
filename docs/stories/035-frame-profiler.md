# 035 - Per-phase frame profiler

Counterpart of [ReHLDS MT-002](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-002-frame-profiler.md).

## Context
`Host_Frame` (`engine/common/host.c`) and `Host_ServerFrame`
(`engine/server/sv_main.c`) have no timing beyond `host.pureframetime`.
Every later story is accepted on before/after numbers.

## Goal
`sv_profile <seconds>` records, for every frame, the time spent in each phase
and prints average, p50, p95, p99, max and standard deviation, plus the tick
interval and per-frame counters (packets in/out, bytes, clients sent,
entities sent). `sv_profile_csv <file>` saves the raw frames.

## Scope
Phases: `frame`, `commands` (`Host_GetCommands`), `sv_frame`, `read_packets`
(`connectionless`, `netchan_process`, `execute_client_msg`), `physics`
(`SV_RunGameFrame`), `check_timeouts`, `send_messages` (`gather`, `encode`,
`transmit` once 045 splits them), `heartbeat`, `log_write`.
Same output format as ReHLDS, so the report tools can be shared.

## Tasks
- [x] Ring buffer of per-frame records (`sv_profile_frames`), monotonic clock
      (`engine/server/sv_profile.c`, a C port of the ReHLDS profiler).
- [x] Phase markers at the call sites above; per-frame counters. `steam` is
      `heartbeat` here (`NET_MasterHeartbeat`), the other columns keep their
      ReHLDS names.
- [x] `sv_profile`, `sv_profile_enable`, `sv_profile_reset`, `sv_profile_csv`.
- [ ] Overhead measured with the profiler on and off.

## Progress (2026-10-15)
Baseline of the 034 server, 19 bots, `de_dust2`, `sys_ticrate 1000`, 30 s:

```
sv_profile: 29553 frames over 30.00 s, 985.1 fps (sys_ticrate 1000), busy 8.0%
time (us)                      avg       p50       p95       p99       max    stddev
tick_interval               1015.1     999.0    1062.5    1145.6   12396.9     244.6
frame                         81.7      31.7     317.1     964.0   12396.6     315.0
    physics                   77.3      27.6     311.7     958.9   12392.7     315.0
filter_rejects              2702.1      1112     11327     18654     19896
```

- The server misses 1.5 % of its ticks and the main loop spins between them:
  about 2700 rejected host frames per accepted one (`Host_Autosleep`), see 038.
- Without players Xash3D hibernates (`sv_hibernate_when_empty 1`, bots not
  counted): 29 fps, 50 ms frames, the bots playing at 20 Hz. The 034 server
  turns it off.

## Acceptance criteria
- Overhead under 1 % of the frame at 1000 Hz.
- Output parsed by the ReHLDS report script unchanged.

## Dependencies
034.
