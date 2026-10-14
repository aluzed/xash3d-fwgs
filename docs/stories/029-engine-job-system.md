# 029 - Engine job system and asynchronous loading

## Context
Beyond the renderer, the engine can use threads where it does not affect
simulation results or network behaviour. Server simulation, prediction and
game DLL callbacks remain single-threaded and deterministic: the GoldSrc
protocol and game DLLs assume it.

## Goal
A small job system shared by engine and renderer, used for asynchronous
asset loading and other side-effect-free work.

## Tasks
- [ ] Job system in `engine/common` (fixed worker pool, job handles,
      main-thread completion callbacks); exposed to renderers through a new
      `ref_api_t` entry (`REF_API_VERSION` bump).
- [ ] Asynchronous file reads / decompression for textures, models and
      sounds (precache still completes before the client signals ready, so
      the connection sequence with the server is unchanged).
- [ ] Texture streaming backend for 022.
- [ ] Optional: sound mixing on its own thread if the profiler shows benefit.
- [ ] Audit: list every candidate and document why simulation, prediction,
      `CL_Move`, delta decoding and game DLL entry points stay on the main
      thread.

## Progress (2026-10-14)
- [x] Job system core, pulled forward for 022: `engine/common/jobs.c`,
      a fixed pool of workers (`-jobthreads N`, default CPU count - 1 up
      to 4, 0 runs jobs on the spot) on SDL threads, pthreads or Win32
      threads; `done` callbacks run on the main thread at the start of
      `Host_Frame` or in `Job_WaitIdle`. Exposed to renderers as
      `Job_Submit` / `Job_WaitIdle` (`REF_API_VERSION` 22).
- First user: ref_vk computes the light directions of the lightmaps
  (022) on a worker at map load.
- Left: asynchronous file reads for textures, models and sounds, the
  texture streaming backend, the audit, map load timings.

## Acceptance criteria
- Map load time reduced on large maps; no change in connection sequence
  (verified against stock HLDS with packet capture).
- No data races under ThreadSanitizer on the amd64 build.

## Dependencies
020 (can start in parallel with phase 6).
