# 005 - Vulkan instance, device and debug tooling

## Goal
Create the instance, pick a physical device, create the logical device and
queues, with first-class debugging from day one.

## Tasks
- [x] Load entry points through the proc address supplied by the engine
      (volk-style loader in `vk_core.c`, no static libvulkan link).
- [x] Target Vulkan **1.3** baseline (dynamic rendering, synchronization2,
      descriptor indexing, buffer device address); refuse to start below it
      with a clear message so the engine falls back to `ref_gl`.
- [x] Device selection: prefer discrete GPU; `r_vk_device <index>` override;
      log all devices and their RT capability.
- [x] Optional extensions detected and recorded in a `vk_caps` struct:
      `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
      `VK_KHR_ray_tracing_pipeline`, `VK_EXT_memory_budget`,
      texture compression BC.
- [x] Validation layers and `VK_EXT_debug_utils` behind `r_vk_validation`,
      object names on every resource, GPU labels per render pass
      (RenderDoc / Nsight friendly). `-vkvalidation` does the same; debug
      utils (names, labels) are enabled whenever available, even without
      validation.
- [x] Graphics + present queue; dedicated transfer queue if available.

## Acceptance criteria
- Starts with validation enabled and zero validation messages.
- Clean shutdown (no leaked objects reported by validation).

## Results (2026-10-14)
- RTX 2080 Ti picked (discrete, Vulkan 1.4, ray query + pipeline), llvmpipe
  listed; graphics family 0, dedicated transfer family 1.
- `-vkvalidation`: HL c1a0 load, play and quit with 0 validation messages,
  no leak reported at shutdown.
- Optional features enabled when present: maintenance4, runtime descriptor
  arrays, partially bound / variable count / update-after-bind bindings,
  non-uniform indexing, timeline semaphores, scalar block layout,
  anisotropy, BC compression. RT extensions are only recorded (story 024).

## Dependencies
004.
