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
- [x] Queue of packets (`engine/common/net_thread.c`): an 8 MB byte ring under
      a mutex, `-netthread_queue` packets at most (1024), so that a 64 KB
      packet doesn't cost a 64 KB slot. A lock-free queue wasn't needed: the
      thread pushes only what passes the filter.
- [x] Thread on the server sockets only (IPv4 and IPv6, `poll` with a wake
      pipe, `select` on Windows); loopback, fake lag and the client unchanged.
- [x] Copy of the filter state published by the main thread: IP filter list
      (`SV_IPFilterVersion`, `SV_IPFilterList`), client addresses, rate. The
      address of a client being given its slot is published before
      `client_connect` is sent.
- [x] Split packets: the server never receives any (only the client
      reassembles them), nothing to move.
- [ ] `sys_wake_on_packet` (MT-102): not ported. On ReHLDS it took the
      commands out of the tick without lowering the latency players see.

Filtering (`net_thread_filter 1`) drops connectionless packets from banned
addresses, challenges, connects and queries (A2S, `info`, pings) above the
rate limits of `SV_CheckRateLimit` (buckets of the thread's own; the main
thread's limits still apply), and game packets from addresses that aren't a
client's IP (the check of `SV_ReadPackets`, which ignores the port). Other
connectionless packets (rcon, master server, the game library's own commands
through `pfnConnectionlessPacket`) always reach the main thread.

Options: `-netthread` (off by default), `-nonetthread` (wins, so that a
command line can turn it off again), `-netthread_queue`, `net_thread_filter`,
`net_thread_stats`.

## Progress (2026-10-15)
19 bots, 50 000 packets per second from 4096 addresses, 30 s, every other
option on:

| Flood | `-netthread` | fps | frame avg | `read_packets` avg / p99 | packets read by the main thread per frame | CPU |
|---|---|---|---|---|---|---|
| junk game packets | no | 990.3 | 150 µs | 62.5 / 194 µs | 50 | 35 % |
| junk game packets | yes | 983.5 | 98 µs | 0.4 / 0.9 µs | 0 | 58 % |
| A2S_INFO queries | no | 982.4 | 199 µs | 110 / 692 µs | 50 | 40 % |
| A2S_INFO queries | yes | 989.0 | 118 µs | 28 / 639 µs | 4 | 61 % |

- Junk is gone from the main thread. The queries the per-address limits
  let through (4096 sources at 1 per second) are still answered by the main
  thread, up to 600 µs in a frame: story 042 answers them on the thread.
- The thread costs about 20 % of a core under the flood; without one it
  sleeps in `poll`.
- 32 load clients with the thread: 1000.0 fps, tick p99 1000.3 µs, 100
  snapshots per second each, no packet dropped.
- ThreadSanitizer: 8 clients, 4 bots and the A2S flood with the thread and
  the log writer: no report.

## Acceptance criteria
- Under the A2S and junk flood scenarios, the main thread keeps its no-flood
  frame times.
- Byte-identical replays (037) with the thread on.

## Dependencies
035, 036, 037, 038.
