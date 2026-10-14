# 022 - Material system and high-resolution textures

## Context
Xash3D already supports TGA replacements under `materials/`
(`Documentation/hd-textures.md`, `host_allow_materials`). That covers albedo
only and has no compression or streaming.

## Goal
PBR materials and 4K texture packs, loaded efficiently, while keeping the
existing `materials/` layout working.

## Tasks
- [ ] Material definition file (`materials/<...>/<name>.mat`, simple
      key/value): albedo, normal, roughness/metalness/AO packed map,
      emissive, height; per-material flags (emissive strength, reflective).
- [ ] Keep original texture dimensions for UV / gameplay purposes: HD
      textures are only sampled with the original texcoords (GoldSrc UVs
      are in texels of the original size).
- [ ] KTX2 container with BC7 (albedo), BC5 (normal), BC4/BC7 (masks);
      offline compression in tooling (023), no runtime compression.
- [ ] Texture streaming: low mips resident, high mips loaded asynchronously
      (uses 029 job system) under a VRAM budget; mandatory for the 32-bit
      build (address space).
- [ ] Normal mapping and PBR shading in `enhanced` mode, with lightmaps as
      indirect diffuse; `classic` mode ignores materials except albedo.
- [ ] Mod / map overrides resolution order identical to existing doc.

## Acceptance criteria
- A test pack replacing all `de_dust2` textures with 4K PBR materials loads
  without hitch, stays within budget, and works in the i386 build.

## Dependencies
021, 029.
