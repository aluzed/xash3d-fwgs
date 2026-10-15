# 044 - Client of a game packet found by address

Counterpart of [ReHLDS MT-104](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-104-client-lookup-by-address.md).

## Context
`SV_ReadPackets` compares the address of every client slot
(`NET_CompareBaseAdr`) for every game packet.

## Goal
`sv_client_lookup_cache 1`: a table indexed by address, each hit checked
against the slot, kept up to date on connect, disconnect and port change.

## Progress (2026-10-15)
- [x] `sv_client_lookup_cache` (off by default), `SV_FindClientSlot` in
      `engine/server/sv_main.c`; the per-client body of `SV_ReadPackets` moved
      to `SV_ReadClientPacket`.
- Xash3D differs from ReHLDS here: the scan matches the IP only
  (`NET_CompareBaseAdr`) and lets each slot's netchan reject the packet (cookie,
  qport), so that a client whose NAT changed its port is still found. The cache
  hits on the full address; when the slot it gives rejects the packet, or on a
  miss, the original scan runs.
- [ ] Measured with load clients (036).

## Acceptance criteria
- Same client found for every packet (debug check against the scan).
- Packet dispatch time measured before and after.

## Dependencies
035.
