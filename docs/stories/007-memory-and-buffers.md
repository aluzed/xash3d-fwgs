# 007 - Memory, buffers and upload paths

## Goal
Efficient GPU memory management for static and per-frame data.

## Tasks
- [ ] Integrate Vulkan Memory Allocator (`3rdparty/` submodule, C wrapper
      compiled as C++ in its own object) or write a small slab allocator;
      decision recorded here.
- [ ] Static geometry buffers (world, models) uploaded through a staging
      ring; batching of uploads per frame.
- [ ] Per-frame dynamic ring buffer for 2D, TriAPI, particles, beams,
      sprites (persistently mapped, sized by cvar, grows on overflow with a
      warning).
- [ ] Uniform / storage buffers for per-frame and per-view data.
- [ ] Memory budget reporting (`r_vk_meminfo` command), using
      `VK_EXT_memory_budget` when present.
- [ ] Map lifetime: free per-map resources on `R_NewMap` / map change.

## Acceptance criteria
- Loading 20 maps in a row does not grow memory usage.
- Works within a 32-bit process address space (staging kept small, no huge
  persistent mappings).

## Dependencies
005.
