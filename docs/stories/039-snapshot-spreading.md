# 039 - Spread client snapshots over the frames

Counterpart of [ReHLDS MT-106](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-106-snapshot-send-spreading.md).

## Context
`SV_SendClientMessages` (`engine/server/sv_frame.c`) sends a snapshot when
`cl->next_messagetime - (host.realtime + sv.frametime) <= 0`, then sets
`next_messagetime = host.realtime + sv.frametime + next_messageinterval`. Every
client connected in the same frame keeps the same phase, so the snapshots of
many clients bunch into the same frames, and the extra `sv.frametime` makes
the real rate lower than `cl_updaterate`. In ReHLDS this bunching was the
main cause of the long frames on a full server.

## Goal
`sv_snapshot_spread 1`: each client gets a fixed phase within its update
interval, and the send times follow a grid (`next += interval`), so the rate is
exactly `cl_updaterate` and the snapshot work is spread evenly.

## Progress (2026-10-15)
- [x] `sv_snapshot_spread` (off by default): `SV_NextSpreadMessageTime` in
      `engine/server/sv_frame.c`, the ReHLDS formula.
- [ ] Measured with load clients (036): bots receive no snapshots.

## Acceptance criteria
- 32 clients at `cl_updaterate 100` on a 1000 Hz server: each receives 100
  snapshots per second; the `send_messages` p99 drops on the 32 client scenario.

## Dependencies
035, 036, 038.
