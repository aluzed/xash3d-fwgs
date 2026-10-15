# 042 - Server browser answers on the network thread

Counterpart of [ReHLDS MT-305](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-305-a2s-query-cache.md).

## Context
`engine/server/sv_query.c` builds the A2S_INFO, A2S_PLAYER and A2S_RULES
answers (and their challenges) on the main thread for every query; Xash3D
also answers its own `info` / `netinfo` queries (`SV_Info`) there.

## Goal
`sv_a2s_cache 1`: the main thread refreshes a copy of the answers every
second and when players, cvars or the map change; the network thread answers
the queries from it, with their own rate limits and a challenge budget
(`sv_a2s_challenges_sec`).

## Acceptance criteria
- Answers byte-identical to the main thread's for the same state.
- A query flood no longer keeps players from connecting.

## Dependencies
040.
