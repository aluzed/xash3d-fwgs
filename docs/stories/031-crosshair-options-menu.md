# 031 - Customizable crosshair: options page with live preview

## Context
The crosshair cvars of 030 need a page in the options, CS2 style: every
value adjustable precisely, and a preview that shows exactly what will be
drawn in game. The menu is `mainui` (`3rdparty/mainui`, submodule), which
already has sliders, spin controls, check boxes and a colour picker dialog
(`menus/ColorPickerDialog.cpp`). Changes go on a branch of the submodule.

## Tasks
- [x] New "Crosshair" page, reached from the options (next to Player Setup).
      It only shows when the running game provides the cvars
      (`cl_crosshair_style` exists), so Half-Life and other mods are
      unaffected.
- [x] Controls:
      - style: classic, cross, T (no top bar), circle;
      - centre dot on/off, dot size;
      - length, gap, thickness: sliders with a fine step (0.1) plus an
        editable numeric field for exact values, negative gap allowed;
      - crosshair colour: colour picker, R G B fields, alpha slider;
      - outline on/off, outline thickness, outline colour and alpha;
      - dynamic gap on/off and its scale;
      - scale with resolution;
      - anti-aliased edges on/off;
      - "Reset to default" and "Cancel" (restores the values the page opened
        with).
- [x] Live preview panel, drawn with the shared geometry module of 030
      (the same code as the game, not a copy): at real size (1:1, the
      in-game pixel size for the current resolution) and zoomed (x4) to
      check fractional values; switchable background (dark, light, and a
      map-like picture) to judge contrast; a "fire" toggle that animates the
      dynamic gap.
- [x] Changes apply to the cvars live and are saved with the config.
- [x] Translations through the menu's string tables (English first).

## Acceptance criteria
- Any crosshair a player can make with the cvars can be made from the page.
- The preview at 1:1 matches the in-game crosshair pixel for pixel (same
  geometry module, checked with a screenshot of both).
- The page is absent when playing Half-Life.

## Progress (2026-10-14)
Branch `crosshair-options` of `3rdparty/mainui_cpp` in the `cs16-client`
checkout, cvar names as mapped in 030 (`xhair_*`).

- Options > Crosshair is a page of its own (`menus/Crosshair.cpp`), listed
  only when `xhair_color` exists; the old entry under Player Setup is gone.
- Enhanced crosshair on: style (cross, T, circle), length, gap, thickness,
  outline, dot and dot size, scale, smooth edges, additive, colour and
  outline colour (R G B A each), dynamic gap, weapon gap, dynamic scale.
  Every value is a slider plus a field that takes exact values, outside the
  slider range included. Off: the classic size, colour and translucency.
- Colour: R G B A sliders and fields rather than the HSV picker, which
  takes too much room on a 1024x768 page.
- Done keeps the changes and writes the config, Cancel restores the values
  the page opened with, Defaults sets the client's defaults.
- Preview: 1:1 and x4 views, background map / black / light remembered in
  `ui_xhair_background`, "Fire" shoots ten times a second through the
  game's dynamic gap step (`XHair_StepGap`).
- `crosshair_geom.cpp` gained `XHair_SetupParams`, `XHair_ParseColors` and
  `XHair_StepGap`, used by both the HUD and the page; the parent CMake adds
  the file to the menu library (`MAINUI_CROSSHAIR_GEOM`).
- Checked: harness scene `menu_crosshair_cstrike` (black shot) against
  `crosshair_cstrike_menu_match`: the 1:1 view and the game give the same
  205 crosshair pixels, 0 level of difference. GL and VK agree on the
  page; the in-game crosshair captures are unchanged by the refactor.

## Dependencies
030.
