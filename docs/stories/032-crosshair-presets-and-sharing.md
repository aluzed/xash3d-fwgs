# 032 - Customizable crosshair: presets and share codes

## Context
Once players build crosshairs (030, 031), they want to keep several and
exchange them, like CS2 share codes. Optional, after 031.

## Tasks
- [x] Named presets saved in the game directory
      (`crosshair_presets.cfg`), listed on the options page with save,
      rename, delete and apply.
- [x] A few built-in presets: classic 1.6, a small static cross, a T, a
      circle with dot.
- [x] Share code: every parameter packed into a short text code (versioned,
      with a checksum, safe to paste in chat), with console commands
      `crosshair_export` and `crosshair_import <code>` and copy / paste
      buttons on the page.
- [x] Codes from a newer version with unknown fields import what they can
      and say so.

## Acceptance criteria
- Exporting then importing a code gives back exactly the same crosshair.
- Presets survive a game restart.

## Progress (2026-10-14)
In the `cs16-client` checkout: `cl_dll/crosshair_code.{h,cpp}` (branch
`crosshair-custom`) and the presets page in `3rdparty/mainui_cpp`
(branch `crosshair-options`).

- Share code: `XH-` then Crockford base32 groups of five (case and dashes
  ignored, O/I/L read as 0/1). Bytes: format version, the fields that
  differ from the defaults as key (field id and value type) and value,
  CRC-16. A value type in every key lets a reader skip fields it doesn't
  know; numbers with up to three decimals are stored as thousandths,
  others as raw floats, so the crosshair comes back exactly. The default
  crosshair is `XH-07RX2`, a typical custom one about 45 characters.
- Console: `crosshair_export` prints the code and copies it to the
  clipboard (SDL already loaded by the engine, found with
  `dlopen(RTLD_NOLOAD)`: no engine API change); `crosshair_import <code>`
  uses one and reports ignored fields of a newer version.
- Options > Crosshair > Presets: built-in presets (Classic 1.6, Small
  static cross, T with dot, Circle with dot) and the player's, with
  preview, Use (or double click), Save current (the same name replaces),
  Rename, Delete, Copy code, Use code (paste with Ctrl+V). The file is
  `crosshair_presets.cfg` in the game directory, one `"name" "code"` line
  per preset.
- Checked: `tests/crosshair_code_test.cpp` (20000 random crosshairs back
  exactly, through the cvars too; any single changed character is caught;
  a version 2 code with two unknown fields imports the known ones);
  harness: console import then export gives the same code, the presets
  file loads, GL and VK agree on every page.
- Not checked: the page's buttons with a real mouse (save, rename, delete
  write the file through the engine's `COM_SaveFile`).

## Dependencies
031.
