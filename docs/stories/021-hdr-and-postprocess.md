# 021 - HDR, tonemapping and post-process framework

## Goal
A render graph with an HDR pipeline, keeping a "classic" mode that is
pixel-identical to parity output.

## Tasks
- [ ] Minimal render graph (passes, transient attachments, automatic
      barriers) replacing ad-hoc pass code from phases 1-5.
- [ ] HDR main target (`R16G16B16A16_SFLOAT`), lightmaps decoded to linear.
- [ ] Tonemappers (classic passthrough, ACES, AgX) and auto exposure.
- [ ] Bloom, SSAO (fallback for non-RT GPUs), FXAA/SMAA, sharpen.
- [ ] `r_vk_quality classic|enhanced|rt` master switch plus per-feature cvars.

## Progress (2026-10-14)
- [x] `r_vk_quality classic|enhanced|rt` plus per-feature cvars (see
      `Documentation/ref-vk.md`); `rt` renders as `enhanced` until 024.
- [x] HDR scene target (`R16G16B16A16_SFLOAT`) sharing the main depth: the
      imm and world pipelines exist per colour target, the HDR ones
      (built on first use) decode every colour with a gamma 2.2 power by
      specialization, so the GL blending stays exact and lightmaps,
      textures and vertex colours are linear. Overbright lighting keeps
      its range above 1.
- [x] Tonemappers: clamp, ACES (Hill), AgX (minimal), neutral (Khronos PBR
      Neutral's shoulder without its toe, the default: classic colours
      below 0.76). ACES and AgX are calibrated to keep HL / CS middle tones.
      Auto exposure from the log average luminance, adapted over time,
      never above the classic exposure in multiplayer.
- [x] Bloom (13-tap down, tent up), SSAO (half size, depth only), FXAA,
      contrast adaptive sharpening. SMAA not done.
- [~] Render graph: the post chain runs through a minimal frame graph
      (passes name what they sample and write; barriers, layout
      transitions and descriptor sets follow). The scene passes still use
      `VK_BeginMainPass`, now with a switchable colour target; moving them
      into the graph is left for 024, when the RT passes need it.
- Classic: the full harness is identical to the ref_vk captures before
  the story (69/69, only the animated menu banner differs) and 69/69
  above 40 dB against ref_gl. `hdr_valve` / `hdr_cstrike` shot scenes show
  each effect alone, then all of them, then classic again in one run.
- Cost: see `Documentation/ref-vk.md` (0.46 ms at 720p on the 2080 Ti).

## Acceptance criteria
- `classic` mode passes the parity harness unchanged.
- Each effect toggles at runtime without a restart.

## Dependencies
020.
