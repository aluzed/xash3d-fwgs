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
- [ ] Protocol 49 load client: connect, challenge, signon, play (movement and
      fire), acknowledge snapshots, measure the update rate and input latency.
- [ ] Scenarios and report reused from ReHLDS where the protocol does not matter
      (floods, bots, report format).
- [ ] Reference results of the current engine in `docs/benchmarks/`.

## Acceptance criteria
- 32 load clients stay connected for 10 minutes with the expected update rate.
- Two runs of the same build agree within the noise the report states.

## Dependencies
034, 035.
