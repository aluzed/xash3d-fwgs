# 040 - Network receive thread and early filtering

Counterpart of [ReHLDS MT-302](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-302-network-receive-thread.md),
[ReHLDS MT-303](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-303-early-packet-filtering.md) and
[ReHLDS MT-102](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-102-wake-on-packet.md).

## Context
`SV_ReadPackets` (`engine/server/sv_main.c`) drains the socket on the main
thread through `NET_GetPacket` → `NET_QueuePacket` (`engine/common/net_ws.c`),
which also reassembles split packets (`NET_GetLong`) and runs the lag
simulation. Connectionless packets (queries, challenges, rcon) and junk are
handled in the frame, so a flood costs tick time. On ReHLDS, the stock engine
fell to 45 fps under a 50 000 queries/s flood.

## Goal
A dedicated thread reads the server sockets (IPv4 and IPv6) and queues the
packets for the main thread; it drops on the spot what the main thread would
ignore: banned addresses (`engine/server/sv_filter.c`), queries above the rate
limits, malformed packets, netchan packets from addresses that are not
connected. Options: `-nonetthread`, `-netthread_queue`, `net_thread_filter`,
`net_thread_stats`; with the scheduler, `sys_wake_on_packet` reads packets as
they arrive between ticks.

## Tasks
- [ ] Lock-free single producer / single consumer queue of packets.
- [ ] Thread on the server socket only; client and loopback paths unchanged.
- [ ] Snapshot of the filter state (bans, connected addresses) published by
      the main thread.
- [ ] Split packet reassembly stays on the main thread or moves with the
      filter, decided with numbers.

## Acceptance criteria
- Under the A2S and junk flood scenarios, the main thread keeps its no-flood
  frame times.
- Byte-identical replays (037) with the thread on.

## Dependencies
035, 036, 037, 038.
