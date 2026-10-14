# 020 - Parity sign-off and making `ref_vk` selectable

## Tasks
- [ ] Full regression harness pass with `ref_vk` vs `ref_gl`; triage every
      diff (fixed, or accepted with justification).
- [ ] Full HL campaign playthrough (save/load, changelevel) and CS session:
      listen server, Xash dedicated server, stock HLDS/ReHLDS server.
- [ ] Performance: `timedemo` at least equal to `ref_gl` on the reference
      GPU (RTX 2080 Ti) and on an integrated GPU.
- [ ] Renderer selectable in the video options menu; automatic fallback to
      `ref_gl` if Vulkan init fails.
- [ ] Documentation `Documentation/ref-vk.md` (cvars, requirements, known
      issues).

## Acceptance criteria
- Zero validation errors over a full playthrough.
- No crash over 2 h of CS online play.

## Dependencies
010-019, 028.
