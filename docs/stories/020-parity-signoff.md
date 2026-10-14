# 020 - Parity sign-off and making `ref_vk` selectable

## Tasks
- [ ] Full regression harness pass with `ref_vk` vs `ref_gl`; triage every
      diff (fixed, or accepted with justification).
- [ ] Full HL campaign playthrough (save/load, changelevel) and CS session:
      listen server, Xash dedicated server, stock HLDS/ReHLDS server.
- [~] Performance: `timedemo` at least equal to `ref_gl` on the reference
      GPU (RTX 2080 Ti) and on an integrated GPU.
- [x] Renderer selectable in the video options menu; automatic fallback to
      `ref_gl` if Vulkan init fails.
- [x] Documentation `Documentation/ref-vk.md` (cvars, requirements, known
      issues).

## Progress (2026-10-14)
- Harness: 44/44 captures above 40 dB against ref_gl, 0 validation
  messages on the scenes checked; the remaining gaps are listed in
  `Documentation/ref-vk.md`.
- timedemo, RTX 2080 Ti: HL demos 15-18 % faster than ref_gl, CS demos
  48-57 % faster (non-blocking uploads, swapchain created at init). Not
  measured on an integrated GPU yet.
- "Vulkan" is in the renderer list after OpenGL; a failed Vulkan init
  falls back to OpenGL and keeps the video settings (the engine used to
  unlink its own video cvars when a renderer failed).
- Gaps closed after the first sign-off pass: `envshot` / `skyshot`
  (and views smaller than the screen, which landed at the top), stencil
  tested `r_shadows`, `r_speeds`, timedemo 1 % / 0.1 % lows. New static
  scenes: `cubemap_valve`, `funcwater_valve`, `wateralpha_valve` (with a
  player spray), `gratedecal_valve`, `shadows_valve`; full harness 69/69
  above 40 dB before them.
- Left to do with the user: HL campaign and CS sessions on real servers,
  2 h of online play, an integrated GPU.

## Acceptance criteria
- Zero validation errors over a full playthrough.
- No crash over 2 h of CS online play.

## Dependencies
010-019, 028.
