# 012 - BSP world geometry and lightmaps

## Goal
Convert loaded BSP models (v30 GoldSrc, plus BSP2 / Quake formats Xash3D
supports) into GPU-friendly static buffers with lightmap atlases.

## Tasks
- [ ] Implement `Mod_ProcessRenderData` for brush models: build per-surface
      vertex/index ranges once per map in a single static buffer.
- [ ] Lightmap atlas packing (multiple pages as a texture array); 4 lightstyle
      slots per surface uploaded separately so styles can animate on GPU.
- [ ] Texture chains grouped by texture for batching; keep per-surface
      metadata (flags, texinfo) in a storage buffer for later RT use (024).
- [ ] Respect `gl_overbright` / lightmap scale behaviour of GoldSrc.

## Acceptance criteria
- A static view of `c1a0` and `de_dust2` with world only matches `ref_gl`.
- Map load time no worse than `ref_gl` + 10 %.

## Dependencies
009.
