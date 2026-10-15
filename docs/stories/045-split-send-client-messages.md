# 045 - Split `SV_SendClientMessages` into gather, encode and transmit

Counterpart of [ReHLDS MT-401](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-401-split-send-client-messages.md).

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

## Dependencies
035, 037.
