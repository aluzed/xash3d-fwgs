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

## Progress (2026-10-14)
- [x] `.mat` files next to the TGA replacements (same directories and
      order as the engine, `host_allow_materials`): albedo, normal, rma,
      emissive, height, `emissive_strength`, `reflective`
      (`ref/vk/vk_material.c`, `Documentation/hd-textures.md`).
- [x] Maps sampled with the original texture coordinates, any size.
- [x] KTX2 with BC4 / BC5 / BC7 (and DDS): ref_vk now advertises
      `IL_DDS_HARDWARE` and maps BC4 / BC5; offline compression is
      `utils/ktx2enc` (023, pulled forward).
- [x] Streaming (`ref/vk/vk_stream.c`): small levels at load, the others
      read on the engine's worker threads (029's job system, pulled
      forward) as the screen size of the texels asks, uploaded at a fixed
      rate, under `r_vk_texture_budget`.
- [x] Normal mapping and PBR in `enhanced`: lightmaps as indirect diffuse
      scaled by the normal map against the light direction worked out at
      map load from the BSP lights (`vk_deluxe.c`, on a worker), GGX
      specular and emission in an additive pass; `classic` uses the
      albedo only. World surfaces only: studio models keep their TGA
      replacements (albedo).
- [x] Test pack: `scripts/texpack.py` makes every de_dust2 texture a 4K
      material (Lanczos upscale and luminance heuristics, 023's pipeline
      in its first form). Loads without hitch (slowest frame 7.7 ms on
      `long_de_dust2`, 6.8 ms without the pack), stays within budget
      (577 of 1024 MB on the materials scene), works in the i386 build
      (`scripts/dev/build-i386.sh`, Docker; cs16-client for CS since the
      current Steam client.so of CS crashes in `HUD_Init` on Xash).

## Acceptance criteria
- A test pack replacing all `de_dust2` textures with 4K PBR materials loads
  without hitch, stays within budget, and works in the i386 build.

## Dependencies
021, 029.
