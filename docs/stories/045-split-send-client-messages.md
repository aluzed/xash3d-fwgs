# 045 - Split `SV_SendClientMessages` into gather, encode and transmit

Counterpart of [ReHLDS MT-401](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-401-split-send-client-messages.md).

## Context
In `engine/server/sv_frame.c`, `SV_SendClientDatagram` does everything for
one client in one pass: `SV_WriteClientdataToMessage` (game DLL
`pfnUpdateClientData`, weapon data), `SV_WriteEntitiesToClient`
(`SV_AddEntitiesToPacket` calls the game DLL's `pfnAddToFullPack` and
`pfnCheckVisibility`, then `SV_EmitPacketEntities` delta encodes, then
events and pings), the multicast datagram, then `Netchan_TransmitBits`.
`frame_ents` is a function-level `static`, `svs.packet_entities` a shared ring.

## Goal
Three steps, still single-threaded: **gather** (every game DLL call, entity
states copied into the client frame), **encode** (pure engine work from the
gathered data into the client's message buffer), **transmit** (netchan).
This is the refactor that 047 parallelises.

## Acceptance criteria
- Byte-identical replays (037).
- The profiler (035) reports `gather`, `encode` and `transmit` separately.

## Progress (2026-10-15)
- [x] `sv_parallel_send 1` (off by default): `SV_SendClientMessages` gathers
      the snapshots of every client due this frame first
      (`SV_GatherClientDatagram`, main thread), then encodes them
      (`SV_EncodeClientDatagram`), then sends them in client order
      (`SV_TransmitSnapshot`). With 0 the original code runs unchanged.
- The gather does everything that calls the game library or changes state:
  `UpdateClientData`, `GetWeaponData`, fixangle and choke, the entity list
  (`AddToFullPack`), the entity buffer, the delta plan of
  `SV_EmitPacketEntities` (baselines chosen, removals, the game's custom
  encoders run and their results kept as masks, see 046), the events and the
  pings (`SV_GetPlayerStats` keeps state), written into a small buffer per
  client. The encode only reads gathered data and engine state.
- A delta frame overwritten by the later gathers of the same frame (the entity
  buffer turning almost all the way around in one frame) makes the client
  skip that snapshot instead of getting a wrong one.
- Engine fix found on the way: `MSG_WriteBits` copied the whole last byte
  of its source (`MSG_WriteUBitLong` doesn't mask its value), so bits left by
  an earlier `MSG_SeekToBit` landed past the end of the message: the padding
  bits of a packet's last byte depended on what was written before. They are
  now zero.
- [x] Replays: a session of 8 load clients and 4 bots recorded with
      `sv_parallel_send 0` replays byte for byte with `sv_parallel_send 1`
      (19 218 packets, 8 destinations).

## Dependencies
035, 037.
