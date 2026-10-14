# 031 - Customizable crosshair: options page with live preview

## Context
The crosshair cvars of 030 need a page in the options, CS2 style: every
value adjustable precisely, and a preview that shows exactly what will be
drawn in game. The menu is `mainui` (`3rdparty/mainui`, submodule), which
already has sliders, spin controls, check boxes and a colour picker dialog
(`menus/ColorPickerDialog.cpp`). Changes go on a branch of the submodule.

## Tasks
- [ ] New "Crosshair" page, reached from the options (next to Player Setup).
      It only shows when the running game provides the cvars
      (`cl_crosshair_style` exists), so Half-Life and other mods are
      unaffected.
- [ ] Controls:
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
- [ ] Live preview panel, drawn with the shared geometry module of 030
      (the same code as the game, not a copy): at real size (1:1, the
      in-game pixel size for the current resolution) and zoomed (x4) to
      check fractional values; switchable background (dark, light, and a
      map-like picture) to judge contrast; a "fire" toggle that animates the
      dynamic gap.
- [ ] Changes apply to the cvars live and are saved with the config.
- [ ] Translations through the menu's string tables (English first).

## Acceptance criteria
- Any crosshair a player can make with the cvars can be made from the page.
- The preview at 1:1 matches the in-game crosshair pixel for pixel (same
  geometry module, checked with a screenshot of both).
- The page is absent when playing Half-Life.

## Dependencies
030.
