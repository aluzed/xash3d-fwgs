# 006 - Swapchain and frame loop

## Goal
Present frames reliably: resize, fullscreen toggle, vsync, minimise.

## Tasks
- [x] Swapchain creation / recreation on `VK_ERROR_OUT_OF_DATE_KHR`,
      `VK_SUBOPTIMAL_KHR` and engine video mode changes.
- [x] Present mode from `gl_vsync` (FIFO / MAILBOX / IMMEDIATE).
- [x] N frames in flight (default 2) with per-frame command pools, fences,
      semaphores and transient allocators.
- [x] Map `R_BeginFrame` / `R_EndFrame` / `R_ClearScreen` onto acquire,
      record, submit, present.
- [x] Offscreen main colour + depth target (rendering never draws straight
      into the swapchain image) so later post-processing and screenshots
      have a stable source.
- [x] Handle zero-sized window (minimised) without spinning or crashing.

## Acceptance criteria
- Clear colour visible; window resize and alt-enter work 100 times in a row
  with no validation error.
- `fps_max` and vsync behave like `ref_gl`.

## Results (2026-10-14)
- Swapchain rebuilt on out of date / suboptimal, drawable size changes
  (checked every frame) and `gl_vsync` changes.
- The "rendered" semaphore is per swapchain image, since presentation holds
  it; per-frame transient allocators come with story 007.
- 100 resizes (`vid_setmode`) then 100 borderless fullscreen toggles with
  `-vkvalidation`: 201 swapchains, 0 validation messages, clean exit.
- `minimize` for 300 frames: no error, 25 % CPU.
- `VID_ScreenShot` reads the offscreen target (mid-frame flush), so the
  regression harness already captures `ref_vk` (black 1280x720 images).
- vsync off: 920-1020 fps timedemo on an empty frame; the frame rate cap
  stays on the engine side, as with `ref_gl`.

## Dependencies
005.
