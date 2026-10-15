# 035 - Per-phase frame profiler

Counterpart of [ReHLDS MT-002](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-002-frame-profiler.md).

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
- [ ] Ring buffer of per-frame records (`sv_profile_frames`), monotonic clock.
- [ ] Phase markers at the call sites above; per-frame counters.
- [ ] `sv_profile`, `sv_profile_enable`, `sv_profile_reset`, `sv_profile_csv`.
- [ ] Overhead measured with the profiler on and off.

## Acceptance criteria
- Overhead under 1 % of the frame at 1000 Hz.
- Output parsed by the ReHLDS report script unchanged.

## Dependencies
034.
