# 036 - Load test harness for the Xash3D protocol

Counterpart of [ReHLDS MT-003](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-003-load-test-harness.md).

## Context
The ReHLDS harness (`tools/bench/` of the fork) drives GoldSrc protocol 48
load clients (`loadclient.py`), bots, HLTV and floods in Docker, and reports
tick rate, jitter, frame phases and the snapshot rate each client receives.
The Xash3D server only accepts protocol 49 clients: different connection
sequence (`SV_ConnectClient`, `SV_Info`), its own delta encoding
(`engine/common/net_encode.c`) and netchan details (`engine/common/net_chan.c`).

## Goal
The same scenarios against the Xash3D dedicated server: idle, 19 bots,
32 playing load clients, 32 clients with many moving entities, A2S and junk
floods; one Markdown report per run, comparable between builds.

## Tasks
- [x] Protocol 49 load client (`scripts/server/bench/loadclient.py`): connect,
      challenge, sign-on (`new`, `spawn`, `begin`), play (movement and fire),
      acknowledge snapshots, measure the update rate and input latency.
- [x] Runs (`scripts/server/bench/bench.sh`): load clients, bots, floods
      (`flood.py` from ReHLDS, unchanged: A2S queries are the same), the
      profiler and the CPU of the process; results in `~/xash-dev/bench/`.
- [x] Reference results in [`../benchmarks/README.md`](../benchmarks/README.md).

## Progress (2026-10-15)
- Differences from the GoldSrc client: no munge, no BZ2 but LZSS on fragment
  chains, a qport word, fragment offsets and lengths in bits, a `changed` bit
  per `usercmd_t` field, signed fields with the sign bit last. The netchan
  cookie and split packets are not asked for (`ext 0`).
- Each client binds its own `127.0.1.x`: the server allows 1 challenge and
  1 connect per second and 3 sign-ons at once per address.
- The clients send at random phases: connected a whole number of intervals
  apart, they sent their commands in the same frame.
- 32 clients stay in game with exactly 100 snapshots per second; the load
  clients' kills in the log show that their moves pass the checksum.
- Not done: the 10 minute run and the run-to-run noise.

## Acceptance criteria
- 32 load clients stay connected for 10 minutes with the expected update rate.
- Two runs of the same build agree within the noise the report states.

## Dependencies
034, 035.
