# 008 - Shader build pipeline and pipeline management

## Goal
GLSL shaders compiled to SPIR-V at build time and pipelines created
predictably without in-game stutter.

## Tasks
- [x] waf task compiling `ref/vk/shaders/*.{vert,frag,comp,rgen,rchit,rmiss}`
      with `glslc` (or `glslangValidator`) into embedded C arrays
      (`glslc -mfmt=c`, generated in its own waf group so gccdeps sees the
      headers before the C files that include them).
- [x] Shared GLSL headers for bindings and structs, mirrored by a C header
      (single source of truth for binding numbers): `shaders/bindings.h` is
      included by both.
- [x] Bindless texture descriptor set (descriptor indexing) used by all
      passes. 8192 combined image samplers, partially bound, update after
      bind; the index is the engine texture number.
- [x] Pipeline objects keyed by state (render mode, blend, depth, cull);
      all variants built at init; `VkPipelineCache` persisted to the
      writable game dir (`vulkan_pipelines.bin`, ignored when the device or
      driver changed). So far: immediate mode, 3 topologies x 4 blends.
- [ ] Optional dev mode: hot-reload SPIR-V from disk (`r_vk_shader_reload`).

## Acceptance criteria
- Shader edit -> `./waf build` -> visible change, no manual step.
- No pipeline creation during gameplay (logged if it happens).

## Results (2026-10-14)
- Clean build generates the SPIR-V before compiling `vk_pipeline.c`.
- Hot reload (`r_vk_shader_reload`) not done yet.

## Dependencies
005.
