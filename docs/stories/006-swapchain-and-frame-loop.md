# 006 - Swapchain and frame loop

## Goal
Present frames reliably: resize, fullscreen toggle, vsync, minimise.

## Tasks
- [ ] Swapchain creation / recreation on `VK_ERROR_OUT_OF_DATE_KHR`,
      `VK_SUBOPTIMAL_KHR` and engine video mode changes.
- [ ] Present mode from `gl_vsync` (FIFO / MAILBOX / IMMEDIATE).
- [ ] N frames in flight (default 2) with per-frame command pools, fences,
      semaphores and transient allocators.
- [ ] Map `R_BeginFrame` / `R_EndFrame` / `R_ClearScreen` onto acquire,
      record, submit, present.
- [ ] Offscreen main colour + depth target (rendering never draws straight
      into the swapchain image) so later post-processing and screenshots
      have a stable source.
- [ ] Handle zero-sized window (minimised) without spinning or crashing.

## Acceptance criteria
- Clear colour visible; window resize and alt-enter work 100 times in a row
  with no validation error.
- `fps_max` and vsync behave like `ref_gl`.

## Dependencies
005.
