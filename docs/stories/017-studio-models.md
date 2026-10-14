# 017 - Studio models

## Context
CS 1.6 and HL client DLLs ship their own `CStudioModelRenderer` and call the
engine through `engine_studio_api_t` (`R_StudioFillAPI`). The renderer must
honour every callback exactly; bone setup is driven by the client DLL.

## Tasks
- [ ] Implement the `engine_studio_api_t` / `r_studio_interface_t` functions
      used by client DLLs (setup bones, draw points, lighting, chrome,
      remap colours, `StudioSetRenderamt`, `StudioSetupLighting` etc.).
- [ ] GPU skinning: bone matrices to a per-frame storage buffer, vertex
      shader skinning; CPU fallback path kept for debugging.
- [ ] Studio flags: chrome, flatshade, fullbright, additive, masked,
      `STUDIO_NF_*`; player top/bottom colour remap; `r_fullbright`
      remains cheat-protected.
- [ ] GoldSrc lighting model (ambient + shade + `lightgamma`), elights,
      `R_LightPoint` sampling.
- [ ] View model: separate depth range / FOV (`viewmodel_fov`-like
      behaviour as `ref_gl`), drawn after world.
- [ ] Glow shells (`kRenderFxGlowShell`), player shadows if `ref_gl` has them.
- [ ] Studio decals.

## Acceptance criteria
- HL scientists, barney, weapons; CS player models with team skins, hostages,
  weapon view models, C4, shields render like `ref_gl`.
- No change in hitboxes or animations (they are server/client DLL logic).

## Dependencies
013, 009.
