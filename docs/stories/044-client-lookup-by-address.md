# 044 - Client of a game packet found by address

Counterpart of [ReHLDS MT-104](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-104-client-lookup-by-address.md).

## Context
`SV_ReadPackets` compares the address of every client slot
(`NET_CompareBaseAdr`) for every game packet.

## Goal
`sv_client_lookup_cache 1`: a table indexed by address, each hit checked
against the slot, kept up to date on connect, disconnect and port change.

## Acceptance criteria
- Same client found for every packet (debug check against the scan).
- Packet dispatch time measured before and after.

## Dependencies
035.
