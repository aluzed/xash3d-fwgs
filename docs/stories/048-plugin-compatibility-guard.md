# 048 - Metamod plugins and the threaded encoding

Counterpart of [ReHLDS MT-404](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-404-plugin-compatibility-guard.md).

## Context
In ReHLDS, the game DLL's conditional encoders ran Metamod's callbacks on the
encoding threads while the main thread ran game DLL code: the heap was
corrupted and the server crashed at the next map change. Metamod-FWGS hooks
the same game DLL interfaces.

## Goal
No game DLL or Metamod code on a worker: conditional encoders recorded on the
main thread during the gather (046) and replayed; a serial fallback for any
client whose snapshot a plugin can change while it is encoded.

## Progress (2026-10-15)
- By design no game library or Metamod code runs on a worker: the custom
  delta encoders, which the game calls through Metamod's engine table, run
  during the gather (046); a custom encoder on client or weapon data turns
  the gathering first off.
- [x] The ReHLDS crash scenario: Metamod-FWGS with the CSDM plugin, 8 playing
      load clients, 4 bots, `sv_parallel_send 1 -sys_threads 2`, a map change
      every minute for 3.5 minutes: 3 map changes, no crash, 14 253 snapshots
      encoded by the workers.
- [ ] One hour with players and bots (050).

## Acceptance criteria
- 034's server with Metamod-FWGS runs the map cycle for an hour with
  `sv_parallel_send 1` and 19 bots and players, no crash.

## Dependencies
034, 047.
