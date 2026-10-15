# 041 - Network send thread

Counterpart of [ReHLDS MT-304](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-304-network-send-thread.md) and
[ReHLDS MT-105](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-105-batched-socket-syscalls.md).

## Context
`Netchan_TransmitBits` ends in `NET_SendPacketEx` (`engine/common/net_ws.c`),
one `sendto` per packet (several for split packets) on the main thread.

## Goal
The main thread queues the server's packets; a dedicated thread sends them,
a tick's packets in one `sendmmsg` call on Linux. Options: `-nonetsend`,
`-netsend_queue`, `net_send_stats`.

## Acceptance criteria
- The transmit phase of the frame drops on the 32 client scenario.
- Packets leave in the same order with the same bytes (037).

## Progress (2026-10-15)
- [x] `engine/common/net_send_thread.c`: `-netsend` (off by default,
      `-nonetsend` wins), `-netsend_queue` (1024 packets, 8 MB),
      `net_send_stats`. `NET_SendPacketEx( NS_SERVER )` queues the packet, the
      thread sends the queue in order, consecutive packets on one socket in
      one `sendmmsg` call on Linux (`sendto` elsewhere). A full queue makes
      the main thread wait: nothing is dropped or reordered.
- [x] Split packets: `NET_SendLong` was split into `NET_NeedsSplit`,
      `NET_NextSplitSequence` (main thread: the number stays in order with the
      synchronous path) and `NET_SendSplit` (the thread sends the parts).
      Xash3D sleeps 100 µs between the parts of a split packet: on the main
      thread, a full update split in 5 parts stopped the frame for 400 µs.
- [x] Measured, 32 load clients, every other option on: `transmit` 47.8 →
      18.9 µs per frame, frame max 2823 → 1336 µs. With the clients asking for
      split datagrams like real clients (`cl_dlmax 1200`, `SPLIT=1`), the
      `transmit` maximum (full updates when the clients join) 2071 → 144 µs.
- [x] Same bytes: the replay records the sends before they are queued
      (037); the load clients receive their 100 snapshots per second.
  ThreadSanitizer with split datagrams, the receive thread and a flood: no
  report.

## Dependencies
040.
