# 017 - Studio models

## Context
CS 1.6 and HL client DLLs ship their own `CStudioModelRenderer` and call the
engine through `engine_studio_api_t` (`R_StudioFillAPI`). The renderer must
honour every callback exactly; bone setup is driven by the client DLL.

## Tasks
- [x] Implement the `engine_studio_api_t` / `r_studio_interface_t` functions
      used by client DLLs (setup bones, draw points, lighting, chrome,
      remap colours, `StudioSetRenderamt`, `StudioSetupLighting` etc.).
- [ ] GPU skinning: bone matrices to a per-frame storage buffer, vertex
      shader skinning; CPU fallback path kept for debugging.
- [x] Studio flags: chrome, flatshade, fullbright, additive, masked,
      `STUDIO_NF_*`; player top/bottom colour remap; `r_fullbright`
      remains cheat-protected.
- [x] GoldSrc lighting model (ambient + shade + `lightgamma`), elights,
      `R_LightPoint` sampling.
- [x] View model: separate depth range / FOV (`viewmodel_fov`-like
      behaviour as `ref_gl`), drawn after world.
- [x] Glow shells (`kRenderFxGlowShell`), player shadows if `ref_gl` has them.
- [ ] Studio decals.

## Acceptance criteria
- HL scientists, barney, weapons; CS player models with team skins, hostages,
  weapon view models, C4, shields render like `ref_gl`.
- No change in hitboxes or animations (they are server/client DLL logic).

## Progress (2026-10-14)
- `vk_studio.c` is ref_gl's `gl_studio.c` with the GL calls replaced: bones,
  lighting, chrome, remap colours, events, gait, view model, glow shells,
  `r_shadows` and the debug draws (`r_drawentities 2..7`) are the same
  code. The triangles go through `vk_imm.c`, which now emulates the GL
  state studio models use (depth test, mask and function, culling, flat
  shading, `GL_REPLACE`, depth range) with Vulkan 1.3 dynamic state.
- Skinning and lighting run on the CPU like ref_gl; GPU skinning is not done.
- `r_shadows` (off by default) draws without ref_gl's stencil test, so
  overlapping shadow triangles may darken twice.
- Studio decals wait for story 016. The client's 3D TriAPI is story 019.
- Harness: 38/38 above 40 dB against ref_gl with the view models on,
  0 validation messages.

## Dependencies
013, 009.
