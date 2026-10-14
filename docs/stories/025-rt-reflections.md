# 025 - RT shadows, AO and reflections

## Tasks
- [x] Ray-traced shadows for dynamic lights and sun (`light_environment`).
      The light the lightmaps have from them leaves the texel where an
      entity stands in between (`vk_rtfx.c`): the bake has the world's
      shadows. Hard shadows: soft ones wait for the denoiser (027).
- [x] Ray-traced ambient occlusion. In place of SSAO, accumulated over
      the frames (reprojection) and filtered: the part of 027 it needs.
- [x] Reflections on water, glass and materials flagged reflective (022).
      Mirror reflections (rippled on water); rough ones wait for 027.
- [x] Studio models receive world shadows and lighting consistent with
      lightmaps. Models lit by the sky lose its directional light where
      the sun is hidden; dynamic lights still light them unshadowed.

## Competitive fairness (multiplayer)
RT reflections and shadows can reveal players that GoldSrc would never show
(around corners, through reflective floors). In multiplayer:
- [x] Player models are excluded from reflection/shadow rays unless they are
      visible to the viewer through the normal PVS / server-sent entities
      (the client never knows more than the server sends, but rendering must
      not amplify it). Stricter: online, a model the eye can't see past
      the world is left out of every ray (`rt_visibility.comp`).
- [x] Server-side opt-out: honour a server cvar / `ServerInfo` key to force
      `classic` in competitive servers. Physinfo key `maxlook`
      (`classic` / `enhanced`), set by a plugin on HLDS / ReHLDS or by
      `sv_maxlook` on a Xash server.

## Acceptance criteria
- Visible improvement, toggleable per feature, denoised with 027.

## Results
- `de_dust2` with 31 bots: players' shadows as dark as the baked ones
  next to them; models out of sight marked (`r_vk_rt_debug 6`).
- With nothing in the way, shadows change no pixel; classic and enhanced
  captures identical to before.
- RTX 2080 Ti, 1280x720: effects 0.6-0.7 ms; `long_c2a5` 606 fps, 1 %
  low 388, `long_de_dust2` 609 fps (enhanced: 1119 and 1207).
- i386 build: `rt` renders as enhanced (no ray tracing in the 32-bit
  driver). No validation message.
- Menu entry for `rt` left to 027 (presets and per-feature options).

## Dependencies
024, 027.
