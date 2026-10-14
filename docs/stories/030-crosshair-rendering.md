# 030 - Customizable crosshair: rendering (Counter-Strike)

## Context
CS 1.6 draws its crosshair in the client DLL (`cs16-client`,
`CHudAmmo::DrawCrosshair` in `cl_dll/ammo.cpp`): four bars from `FillRGBA`
in whole pixels, a colour preset (`cl_crosshair_color`), a size preset
(`cl_crosshair_size`), `cl_crosshair_translucent` and the dynamic gap that
follows the weapon spread (`cl_dynamiccrosshair`). Players want what CS2
offers: build their own crosshair with exact sizes, colours and an outline.

The crosshair is purely client-side: no protocol, server or game DLL change.
The work happens in our `cs16-client` checkout (`~/xash-dev/src/cs16-client`,
upstream Velaron/cs16-client), on a branch of its own.

## Tasks
- [ ] Shared geometry module (C, no dependency, used by this story and by
      the options preview of 031): from the parameters below and the screen
      size, produce the list of quads / ring segments to draw. One source of
      truth, so the preview is exactly the in-game crosshair.
- [ ] Styles (`cl_crosshair_style`):
      - `0` classic: today's 1.6 crosshair, untouched (default);
      - `1` cross: four bars;
      - `2` T: the cross without the top bar;
      - `3` circle: a ring.
- [ ] Parameters, all float where it makes sense:
      - `cl_crosshair_length`: bar length;
      - `cl_crosshair_gap`: distance from the centre to each bar (ring radius
        for the circle), negative allowed (bars overlap the centre);
      - `cl_crosshair_thickness`: bar / ring thickness;
      - `cl_crosshair_dot`, `cl_crosshair_dot_size`: centre dot on/off and
        its size;
      - `cl_crosshair_rgb` ("r g b", 0-255) and `cl_crosshair_alpha`;
      - `cl_crosshair_outline`, `cl_crosshair_outline_thickness`,
        `cl_crosshair_outline_rgb`, `cl_crosshair_outline_alpha`;
      - `cl_crosshair_dynamic`: the gap follows the weapon spread like 1.6
        (`m_flCrosshairDistance`), with `cl_crosshair_dynamic_scale`;
      - `cl_crosshair_scale`: units are pixels at a 1080-pixel-high screen,
        scaled by the screen height times this factor (1 = same apparent
        size at every resolution).
- [ ] Rendering with float geometry: TriAPI quads in screen space (both
      `ref_gl` and `ref_vk` take float 2D vertices), not `FillRGBA`. Edges
      are anti-aliased by coverage (a one pixel alpha ramp on each side of
      the exact edge), so a 1.5 px thickness looks like 1.5 px instead of
      snapping to 1 or 2 px; `cl_crosshair_aa 0` gives hard pixel-snapped
      edges. Ring tessellation adapts to its radius.
- [ ] Outline drawn under the shapes (each shape grown by the outline
      thickness), not over them, so translucent crosshairs keep a clean edge.
- [ ] Keep every existing behaviour: `crosshair 0`, `hud_draw 0`, the
      sniper scope hiding it, spectator modes, the server's `Crosshair`
      user message, `cl_dynamiccrosshair` and the legacy cvars when style 0.
- [ ] Regression: static harness scene with one shot per style (with and
      without outline, fractional values) captured with both renderers;
      style 0 stays identical to the current captures.

## Acceptance criteria
- Every parameter changes the in-game crosshair live, with sub-pixel
  precision visible at 1080p.
- `ref_gl` and `ref_vk` draw the same crosshair (harness above 40 dB).
- With the defaults, nothing changes for existing players.

## Dependencies
010 (2D drawing in `ref_vk`).
