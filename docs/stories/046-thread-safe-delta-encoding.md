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

## Progress (2026-10-15)
Global state map of the encode path (`engine/common/net_encode.c`):
- `Delta_CustomEncode` writes `bInactive` in the shared delta descriptions and
  calls the game's custom encoder (`userCallback`: CS registers `Entity_Encode`,
  `Player_Encode`, `Custom_Encode` for the three entity descriptions), which
  writes them again through `pfnDeltaSetField`/`Unset`, through Metamod's
  engine table: main thread only.
- `Delta_TestBaseline` (best baseline search) does the same: it runs during
  the gather.
- Everything else reads the descriptions, the states and the baselines, and
  writes the message only. `Delta_ClampIntegerField` prints only in debug
  builds; `MSG_WriteDeltaEntity` calls `Host_Error` on an entity number out of
  range, which the gather has already used.
- [x] `Delta_EntityEncoderMask` (main thread) runs the custom encoder and keeps
      its result as a mask; `MSG_WriteDeltaEntityMasked`,
      `MSG_WriteClientDataMasked`, `MSG_WriteWeaponDataMasked` write without
      touching shared state. A custom encoder on client or weapon data (none
      in Counter-Strike) turns the gathering first off.
- [x] TSan with 4 encoding workers, 8 clients, 4 bots, every thread and a
      flood: no report.

## Acceptance criteria
- Global state map of the encode path in this story.
- TSan clean with two encoder threads on the 32 client scenario (037).

## Dependencies
045, 037.
