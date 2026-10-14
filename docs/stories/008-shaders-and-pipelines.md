# 008 - Shader build pipeline and pipeline management

## Goal
GLSL shaders compiled to SPIR-V at build time and pipelines created
predictably without in-game stutter.

## Tasks
- [ ] waf task compiling `ref/vk/shaders/*.{vert,frag,comp,rgen,rchit,rmiss}`
      with `glslc` (or `glslangValidator`) into embedded C arrays.
- [ ] Shared GLSL headers for bindings and structs, mirrored by a C header
      (single source of truth for binding numbers).
- [ ] Bindless texture descriptor set (descriptor indexing) used by all
      passes.
- [ ] Pipeline objects keyed by state (render mode, blend, depth, cull);
      all variants built at init; `VkPipelineCache` persisted to the
      writable game dir.
- [ ] Optional dev mode: hot-reload SPIR-V from disk (`r_vk_shader_reload`).

## Acceptance criteria
- Shader edit -> `./waf build` -> visible change, no manual step.
- No pipeline creation during gameplay (logged if it happens).

## Dependencies
005.
