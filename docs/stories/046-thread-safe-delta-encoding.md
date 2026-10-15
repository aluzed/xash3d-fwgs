# 046 - Thread-safe delta encoding

Counterpart of [ReHLDS MT-402](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-402-thread-safe-delta-encoding.md) and
[ReHLDS MT-203](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-203-thread-safe-engine-services.md).

## Context
`engine/common/net_encode.c` encodes entity states, client data, weapon data
and events through delta descriptions. Encoding may call the game DLL's
conditional encoders (`Delta_InitEncoders`, `pfnCustomEncode`), and uses shared
state: `svs.packet_entities`, baselines (`SV_FindBestBaseline`), message
buffers, `Con_Printf` on errors.

## Goal
Audit and fix every shared write on the encode path so that several clients can
be encoded at once: per-thread scratch, read-only baselines during the encode,
errors deferred to the main thread. Game DLL conditional encoders are run on the
main thread during the gather and their results replayed, as in ReHLDS.

## Acceptance criteria
- Global state map of the encode path in this story.
- TSan clean with two encoder threads on the 32 client scenario (037).

## Dependencies
045, 037.
