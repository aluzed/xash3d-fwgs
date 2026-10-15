# 042 - Server browser answers on the network thread

Counterpart of [ReHLDS MT-305](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-305-a2s-query-cache.md).

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

## Progress (2026-10-15)
- [x] `sv_a2s_cache` (off by default; needs `-netthread` and
      `net_thread_filter 1`): the main thread builds the A2S_INFO and
      A2S_RULES answers with the functions it answers with
      (`SV_SourceQuery_BuildDetails`, `SV_SourceQuery_BuildRules`, split out of
      `sv_query.c`) and the player list, every second and with every other
      configuration change, and publishes them with a copy of the challenge
      salt. The thread answers A2S_INFO, A2S_RULES and A2S_PLAYERS, the two
      last with challenges it computes like the main thread
      (`SV_ComputeChallenge`, the same SipHash, salt and 5 s windows).
- [x] Same bytes as the main thread: A2S_INFO identical, A2S_RULES identical
      but for `mp_timeleft` (the copy is up to a second old), A2S_PLAYERS
      differs by the players' connection times, computed when answering.
- [x] 19 bots, 50 000 A2S_INFO queries per second from 4096 addresses:
      the main thread reads no packet any more, `read_packets` 0.5 µs per
      frame (110 µs without the thread, 28 µs with the thread alone); the
      thread answered 154 000 queries in 34 s, the per-address limits
      dropped the rest. ThreadSanitizer: no report.
- Not done: the challenge budget of ReHLDS (`sv_a2s_challenges_sec`): the
  challenges here go through the per-address limit of `SV_CheckRateLimit`
  only. Xash3D's own `info` and `netinfo` queries (its server browser)
  still go to the main thread, rate limited by the thread.

## Dependencies
040.
