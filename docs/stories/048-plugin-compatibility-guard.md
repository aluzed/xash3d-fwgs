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

## Acceptance criteria
- 034's server with Metamod-FWGS runs the map cycle for an hour with
  `sv_parallel_send 1` and 19 bots and players, no crash.

## Dependencies
034, 047.
