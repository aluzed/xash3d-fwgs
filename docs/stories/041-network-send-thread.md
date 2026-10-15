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

## Dependencies
040.
