# 028 - Renderer multithreading

## Context
`ref_gl` is single-threaded. The engine calls the renderer from the main
thread, and client DLLs call studio / TriAPI functions during the frame, so
those calls must stay on the main thread. The internal architecture of
`ref_vk` does not have to mirror `ref_gl`: only the observable contract
(`ref_api.h`, `engine_studio_api_t`, TriAPI) matters.

## Goal
Use multiple cores for command recording and CPU-side frame preparation
without changing what game DLLs observe.

## Tasks
- [ ] Split the frame into "gather" (main thread, during client DLL calls:
      record draw requests into a frame packet) and "build" (worker threads:
      culling, sorting, command buffer recording into secondary command
      buffers or per-thread pools).
- [ ] Overlap: frame N+1 gather runs while workers build frame N; bounded
      latency (max one frame) and `r_vk_threads 0` for fully synchronous mode.
- [ ] Thread-safe dynamic ring buffers (per-thread sub-allocations).
- [ ] Synchronous paths kept where results are read back immediately
      (`R_LightPoint`, `WorldToScreen`, `ScreenToWorld`, `GetMatrix`,
      screenshots).
- [ ] Profiling markers (Tracy optional) per job.

## Acceptance criteria
- Identical images with `r_vk_threads 0` and `>0`.
- Measurable CPU frame time reduction on crowded CS scenes (32 players,
  smoke, decals).

## Dependencies
013, 017 (design can start after 008; implementation lands before 020).
