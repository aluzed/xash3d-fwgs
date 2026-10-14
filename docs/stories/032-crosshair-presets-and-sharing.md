# 032 - Customizable crosshair: presets and share codes

## Context
Once players build crosshairs (030, 031), they want to keep several and
exchange them, like CS2 share codes. Optional, after 031.

## Tasks
- [ ] Named presets saved in the game directory
      (`crosshair_presets.cfg`), listed on the options page with save,
      rename, delete and apply.
- [ ] A few built-in presets: classic 1.6, a small static cross, a T, a
      circle with dot.
- [ ] Share code: every parameter packed into a short text code (versioned,
      with a checksum, safe to paste in chat), with console commands
      `crosshair_export` and `crosshair_import <code>` and copy / paste
      buttons on the page.
- [ ] Codes from a newer version with unknown fields import what they can
      and say so.

## Acceptance criteria
- Exporting then importing a code gives back exactly the same crosshair.
- Presets survive a game restart.

## Dependencies
031.
