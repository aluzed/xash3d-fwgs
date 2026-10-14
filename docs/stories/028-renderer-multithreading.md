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
- [x] Split the frame into "gather" (main thread, during client DLL calls:
      record draw requests into a frame packet) and "build" (worker threads:
      culling, sorting, command buffer recording into secondary command
      buffers or per-thread pools). Measured first (CPU time per part of
      the frame, `r_vk_gpu_profile`): with 31 bots, the models are 1.3 of
      the renderer's 1.5 ms, three quarters of it making their vertices
      (skinning, lighting, strips to triangles). That moved to workers;
      the draw state and commands stay on the main thread, in order:
      recording them costs little and keeps the GL state emulation simple.
- [x] Overlap: frame N+1 gather runs while workers build frame N; bounded
      latency (max one frame) and `r_vk_threads 0` for fully synchronous mode.
      Overlap inside the frame (the workers make a model's vertices while
      the client library sets up the next ones), no frame of latency:
      online play keeps its input lag.
- [x] Thread-safe dynamic ring buffers (per-thread sub-allocations). The
      main thread reserves the ranges, workers only write them.
- [x] Synchronous paths kept where results are read back immediately
      (`R_LightPoint`, `WorldToScreen`, `ScreenToWorld`, `GetMatrix`,
      screenshots). Untouched; screenshots read the submitted frame.
- [x] Profiling markers (Tracy optional) per job. The main thread's time
      between the profile's marks; no Tracy.

## Acceptance criteria
- Identical images with `r_vk_threads 0` and `>0`.
- Measurable CPU frame time reduction on crowded CS scenes (32 players,
  smoke, decals).

## Results
- Identical captures with `r_vk_threads 0` and 1 (amd64 and i386 with
  Valve's client library), and against the captures before.
- 31 bots in view: the main thread's renderer time 1.55 -> 1.00 ms a
  frame (models 1.32 -> 0.81 ms). No validation message.

## Dependencies
013, 017 (design can start after 008; implementation lands before 020).
