# Vulkan renderer rework (`ref_vk`)

Goal: a new Vulkan renderer for Xash3D FWGS that runs **Half-Life** and
**Counter-Strike 1.6** from a stock Steam install, first at visual parity with
`ref_gl`, then with optional enhanced rendering (HDR, PBR materials, 4K texture
packs, hardware ray tracing).

## Ground rules

1. **Client/server compatibility is untouchable; internals are not.** No
   change to the network protocol, delta encoding, connection sequence,
   game DLL interfaces (`cdll_int`, `eiface`, `engine_studio_api`, TriAPI)
   or simulation results. A `ref_vk` client must join a stock CS 1.6
   (HLDS / ReHLDS) server and a Xash3D server exactly like a `ref_gl`
   client. Everything behind those contracts (renderer architecture, the
   engine/renderer `ref_api.h` contract, threading, asset loading) may be
   redesigned freely.
2. **Original game DLLs must keep working.** Development runs on amd64 with
   `hlsdk-portable` and `cs16-client`, but nothing may break the interfaces
   Valve's 32-bit `client.so` rely on; this is checked with an i386 build
   made inside a container, never on the host. See
   [001](001-baseline-build-and-run.md).
3. **`ref_gl` stays the reference.** Every parity story is validated against
   `ref_gl` screenshots of the same demo frame ([002](002-regression-harness.md)).
4. **Enhanced features are opt-in** behind cvars, can be toggled at runtime,
   and must never give a competitive advantage in multiplayer (see
   [025](025-rt-reflections.md)).
5. **Everything versioned is in English** (code, comments, docs, commits).

## Story format

Each story has: *Context*, *Goal*, *Scope* (in / out), *Tasks*,
*Acceptance criteria*, *Dependencies*, *Risks / notes*. A story is done when
all acceptance criteria pass, the build is warning-free (`werror` target), and
the regression harness shows no new diff outside the story's scope.

## Roadmap

| # | Story | Phase |
|---|-------|-------|
| 001 | [Baseline build and run HL / CS 1.6 on `ref_gl`](001-baseline-build-and-run.md) | 0 - Foundation |
| 002 | [Visual regression harness](002-regression-harness.md) | 0 - Foundation |
| 003 | [Engine: `REF_VULKAN` graphics API and SDL Vulkan window](003-engine-vulkan-window.md) | 0 - Foundation |
| 004 | [`ref_vk` module skeleton](004-ref-vk-skeleton.md) | 0 - Foundation |
| 005 | [Vulkan instance, device and debug tooling](005-instance-and-device.md) | 1 - Core |
| 006 | [Swapchain and frame loop](006-swapchain-and-frame-loop.md) | 1 - Core |
| 007 | [Memory, buffers and upload paths](007-memory-and-buffers.md) | 1 - Core |
| 008 | [Shader build pipeline and pipeline management](008-shaders-and-pipelines.md) | 1 - Core |
| 009 | [Texture manager](009-texture-manager.md) | 1 - Core |
| 010 | [2D drawing: menu, console, HUD](010-2d-drawing.md) | 2 - 2D |
| 011 | [Screenshots, gamma and brightness](011-screenshots-and-gamma.md) | 2 - 2D |
| 012 | [BSP world geometry and lightmaps](012-bsp-geometry-and-lightmaps.md) | 3 - World |
| 013 | [World rendering: visibility, lightstyles, dynamic lights](013-world-rendering.md) | 3 - World |
| 014 | [Brush entities, render modes, water and animated textures](014-brush-entities-and-water.md) | 3 - World |
| 015 | [Sky and fog](015-sky-and-fog.md) | 3 - World |
| 016 | [Decals](016-decals.md) | 3 - World |
| 017 | [Studio models](017-studio-models.md) | 4 - Entities |
| 018 | [Sprites](018-sprites.md) | 4 - Entities |
| 019 | [TriAPI, beams, particles and alias models](019-triapi-beams-particles.md) | 4 - Entities |
| 020 | [Parity sign-off and making `ref_vk` selectable](020-parity-signoff.md) | 5 - Parity |
| 021 | [HDR, tonemapping and post-process framework](021-hdr-and-postprocess.md) | 6 - Enhanced |
| 022 | [Material system and high-resolution textures](022-materials-and-hd-textures.md) | 6 - Enhanced |
| 023 | [Texture upscaling toolchain (4K packs)](023-texture-upscaling-toolchain.md) | 6 - Enhanced |
| 024 | [Ray tracing foundation: acceleration structures](024-rt-foundation.md) | 7 - Ray tracing |
| 025 | [RT shadows, AO and reflections](025-rt-reflections.md) | 7 - Ray tracing |
| 026 | [RT global illumination and light extraction](026-rt-global-illumination.md) | 7 - Ray tracing |
| 027 | [Denoising, temporal upscaling and quality presets](027-denoise-and-presets.md) | 7 - Ray tracing |
| 028 | [Renderer multithreading](028-renderer-multithreading.md) | 1-5 - Cross-cutting |
| 029 | [Engine job system and asynchronous loading](029-engine-job-system.md) | 6 - Cross-cutting |
| 030 | [Customizable crosshair: rendering (Counter-Strike)](030-crosshair-rendering.md) | 8 - Game UI |
| 031 | [Customizable crosshair: options page with live preview](031-crosshair-options-menu.md) | 8 - Game UI |
| 032 | [Customizable crosshair: presets and share codes](032-crosshair-presets-and-sharing.md) | 8 - Game UI |
| 033 | [Valve's current Counter-Strike client library on Xash3D](033-original-cs-client.md) | 0 - Foundation |

Phases 0 to 5 deliver a renderer that is a drop-in replacement for `ref_gl`.
Phases 6 and 7 can be developed in parallel once phase 5 is signed off.
Story 028 is designed early and lands before parity sign-off (020).
Story 023 is offline tooling and can start as soon as 022's material layout
is frozen.
Phase 8 (stories 030-032) is client DLL and menu work, independent of the
renderer phases: it only needs 2D drawing (010).

# Dedicated server multithreading (034-050)

Goal: **a steadier and faster server tick** on the Xash3D FWGS dedicated
server, by bringing over the work of the multithreaded ReHLDS fork
([aluzed/ReHLDS-MT](https://github.com/aluzed/ReHLDS-MT),
stories MT-001 to MT-702): remove waste from the main loop, and move work that
does not touch the game DLL off the main thread. The code cannot be merged:
ReHLDS is a C++ reverse of HLDS, Xash3D an independent C engine. Each story
names its ReHLDS counterpart, whose design and measurements it starts from.

## Ground rules

1. **Measure first.** No optimisation lands without before/after numbers from
   the profiler (035) and the load harness (036), on the server of 034.
2. **The main thread owns the game.** Game DLL and Metamod code only ever runs
   on the main thread; workers only touch engine data.
3. **Byte-identical output.** For the same inputs, the server sends the same
   bytes with every option on or off (037).
4. **Everything is switchable.** Each change has a cvar or command line switch
   and keeps the original code path; options stay off until the field
   validation (050), then turn on by default.
5. **Refactor, then thread.** A parallelisation is preceded by a
   single-threaded refactor that isolates the work (045 before 046-047).
6. Protocol, delta encoding and game DLL interfaces are untouched (ground
   rule 1 above).

## Roadmap

| # | Story | ReHLDS | Phase |
|---|-------|--------|-------|
| 034 | [Dedicated server baseline: Metamod, bots and CSDM](034-dedicated-server-baseline.md) | MT-702 setup | 0 - Measure |
| 035 | [Per-phase frame profiler](035-frame-profiler.md) | MT-002 | 0 - Measure |
| 036 | [Load test harness for the Xash3D protocol](036-load-test-harness.md) | MT-003 | 0 - Measure |
| 037 | [Byte-exact replay and race checks](037-regression-replay-and-race-checks.md) | MT-004 | 0 - Measure |
| 038 | [Deadline-based frame scheduler](038-deadline-frame-scheduler.md) | MT-101, MT-103 | 1 - Tick |
| 039 | [Spread client snapshots over the frames](039-snapshot-spreading.md) | MT-106 | 1 - Tick |
| 040 | [Network receive thread and early filtering](040-network-receive-thread.md) | MT-302, MT-303, MT-102 | 2 - Network |
| 041 | [Network send thread](041-network-send-thread.md) | MT-304, MT-105 | 2 - Network |
| 042 | [Server browser answers on the network thread](042-a2s-answers-on-network-thread.md) | MT-305 | 2 - Network |
| 043 | [Log files written on their own thread](043-async-log-writer.md) | MT-301 | 2 - Network |
| 044 | [Client of a game packet found by address](044-client-lookup-by-address.md) | MT-104 | 2 - Network |
| 045 | [Split `SV_SendClientMessages` into gather, encode and transmit](045-split-send-client-messages.md) | MT-401 | 3 - Snapshots |
| 046 | [Thread-safe delta encoding](046-thread-safe-delta-encoding.md) | MT-402, MT-203 | 3 - Snapshots |
| 047 | [Parallel snapshot encoding on the job system](047-parallel-snapshot-encoding.md) | MT-403 | 3 - Snapshots |
| 048 | [Metamod plugins and the threaded encoding](048-plugin-compatibility-guard.md) | MT-404 | 3 - Snapshots |
| 049 | [Configuration, documentation and launch script](049-configuration-and-docs.md) | MT-701 | 4 - Rollout |
| 050 | [Field validation](050-field-validation.md) | MT-702 | 4 - Rollout |

Order: 034 → 035 → 038 → 039 (re-measure) → 036 → 037 → 040 → 041, 042,
043, 044 → 045 → 046 → 047 → 048 → 049 → 050. The ReHLDS numbers say where
the gains were: the scheduler and snapshot spreading gave the steady tick, the
network thread the flood protection; parallel encoding saved little on real
servers and waits for numbers from this engine.

## Status (2026-10-15)

Done and measured on the 034 server: 034, 035, 036, 037, 038, 039, 040 (but
`sys_wake_on_packet`), 041, 042, 043, 044; 049 for these. Every option is off
by default (ground rule 4) and on with `scripts/server/run.sh`. Results:
[`../benchmarks/README.md`](../benchmarks/README.md); operator guide:
[`../../Documentation/dedicated-server-tuning.md`](../../Documentation/dedicated-server-tuning.md).

Next: 045-048 (parallel snapshot encoding). With 32 players at 100 Hz the
encoding is 56 µs per frame (p99 88 µs) beside 240 µs of game library calls
that stay on the main thread; CS's custom delta encoders (`Entity_Encode`,
`Player_Encode`) write the shared delta descriptions, so the encoders' results
must be recorded on the main thread during the gather, as ReHLDS does.

Not carried over:
- MT-306 (Steam calls on demand): Xash3D has no Steam server API.
- MT-201, MT-202 (thread audit, worker pool): the job system of 029 exists;
  the audit of the encode path is part of 046.
- MT-501, MT-601 (parallel netchan decoding, file transfers): measured not worth
  it on ReHLDS; reconsidered only with numbers from this engine.
