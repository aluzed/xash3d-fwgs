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

Phases 0 to 5 deliver a renderer that is a drop-in replacement for `ref_gl`.
Phases 6 and 7 can be developed in parallel once phase 5 is signed off.
Story 028 is designed early and lands before parity sign-off (020).
Story 023 is offline tooling and can start as soon as 022's material layout
is frozen.
