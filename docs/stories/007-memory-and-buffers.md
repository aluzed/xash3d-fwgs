# 007 - Memory, buffers and upload paths

## Goal
Efficient GPU memory management for static and per-frame data.

## Tasks
- [x] Integrate Vulkan Memory Allocator (`3rdparty/` submodule, C wrapper
      compiled as C++ in its own object) or write a small slab allocator;
      decision recorded here. **Decision: own block allocator**
      (`vk_memory.c`): 64 MB blocks per memory type, first-fit free lists,
      buffers and images in separate blocks (no bufferImageGranularity
      issue), dedicated allocations above 32 MB, host visible blocks mapped
      once. VMA would bring C++ and a wrapper for little gain here.
- [x] Static geometry buffers (world, models) uploaded through a staging
      ring; batching of uploads per frame. 32 MB staging ring, copies
      recorded in an upload command buffer flushed before each frame (and
      when the ring is full); larger uploads get a one-shot buffer. Uses the
      graphics queue for now; the dedicated transfer queue needs ownership
      transfers, worth it once world loading is measured.
- [x] Per-frame dynamic ring buffer for 2D, TriAPI, particles, beams,
      sprites (persistently mapped, sized by cvar, grows on overflow with a
      warning). `r_vk_dynamic_mb`, default 8; an overflow chains a
      second buffer and the slot is rebuilt larger on its next use.
- [ ] Uniform / storage buffers for per-frame and per-view data. The
      dynamic ring already has the uniform / storage usages; per-view data
      comes with the world (story 013).
- [x] Memory budget reporting (`r_vk_meminfo` command), using
      `VK_EXT_memory_budget` when present.
- [x] Map lifetime: free per-map resources on `R_NewMap` / map change.
      Brush and studio model textures are freed in `Mod_ProcessRenderData`
      like ref_gl; GPU objects go through a deferred release queue drained
      once the frames in flight that may use them are done.

## Acceptance criteria
- Loading 20 maps in a row does not grow memory usage.
- Works within a 32-bit process address space (staging kept small, no huge
  persistent mappings).

## Results (2026-10-14)
- 20 CS map loads (5 maps x 4) with `-vkvalidation`: memory and texture
  count identical after map 5 and map 20 (95.3 MB, 1048 textures), the
  growth before that is shared caches filling up as with ref_gl;
  0 validation messages.
- 32-bit constraint: staging is 32 MB and per-frame rings 8 MB, but the
  allocator maps whole host visible blocks; not a concern while amd64 is
  the target (story 001).

## Dependencies
005.
