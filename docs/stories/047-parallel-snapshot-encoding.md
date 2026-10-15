# 047 - Parallel snapshot encoding on the job system

Counterpart of [ReHLDS MT-403](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-403-parallel-encode-and-transmit.md).

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

## Dependencies
046.
