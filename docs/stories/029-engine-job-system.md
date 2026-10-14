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
- [x] Job system in `engine/common` (fixed worker pool, job handles,
      main-thread completion callbacks); exposed to renderers through a new
      `ref_api_t` entry (`REF_API_VERSION` bump).
- [x] Asynchronous file reads: the material maps of ref_vk, their first
      levels included, are read on the workers, the game texture drawn
      until then. Models and sounds stay on the main thread: measured map
      loads are 0.1-0.2 s, see `Documentation/job-system.md`.
- [x] Texture streaming backend for 022 (ref_vk `vk_stream.c` reads on the workers).
- [x] Optional: sound mixing on its own thread: not worth it (`S_Update`
      is small, SDL feeds the device from its own thread).
- [x] Audit (`Documentation/job-system.md`): list every candidate and document why simulation, prediction,
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
- Map loads measured (`Documentation/job-system.md`).
- ThreadSanitizer: `Test_RunJobs` (2000 jobs, done functions submitting
  more, a full queue) passes in the dedicated test build under TSan with
  no report. The whole client can't run under TSan here: the NVIDIA
  driver's threads make TSan abort ("CHECK failed ... thr->slot"), with
  either renderer, so the renderer's two jobs are checked by design
  (their own data only, see the documentation).
- The connection sequence can't have changed: only the renderers use the
  workers, and neither the precache nor the network code was touched.

## Acceptance criteria
- Map load time reduced on large maps; no change in connection sequence
  (verified against stock HLDS with packet capture).
- No data races under ThreadSanitizer on the amd64 build.

## Dependencies
020 (can start in parallel with phase 6).
