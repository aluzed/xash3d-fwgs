# 066 - A console of today: selectable, copyable, readable (engine)

## Context
The console is Quake's, kept by Half-Life and by Xash3D: a curtain of
bitmap font (`fonts.wad`, cp1251) dropping from the top, its text drawn
character by character. One can paste into its input line (Ctrl+V) but
nothing can be selected nor copied out of it: an address, a position
(`viewpos`), an error must be read and typed again. Its font is small,
blurry at 2560 x 1440, and cannot change. It opens only in game unless
the game is started with `-console` (open from the menu since 2026-10-16).

Counter-Strike 2's console is a window: a log with colours, a filter, an
input line with suggestions, text that one selects with the mouse and
copies.

## Goal
The console as a window over the game and the menu: a scrollable log in
a TrueType font sized for the screen, text selected with the mouse
(drag, double click a word, triple click a line) and copied with Ctrl+C or
a right click, Ctrl+A to select all, the input line with suggestions of
commands and cvars and the history, a filter of the log. Same key (²),
same commands; the old curtain no longer drawn.

## Scope
In: the engine's console (`engine/client/console.c`): its drawing, its
mouse and keyboard, the clipboard (SDL), a TrueType font
(`cl_unifont.c` already draws TrueType with stb_truetype).

Out: the notify lines at the top of the game and the chat (they stay as
they are), a console of the dedicated server (its own window).

## Status (2026-10-16)
Written, not started; the look to agree on.

## Design
- A panel taking the top half of the screen by default (CS2's: a window,
  movable and resizable, its place kept in `con_window` cvars), dark and
  translucent, the game seen behind.
- The log: the existing console buffer, drawn line by line in a TrueType
  monospace font (the font shipped with the engine for `cl_unifont`, or
  one of the set's), size from the screen's height (`con_fontsize` in
  pixels at 1080, scaled), colours as now (`^1`..`^9`).
- Selection: the mouse is shown while the console is open; a drag selects
  from character to character across lines, double click a word, triple
  click a line; Ctrl+C or right click copies (`Platform_SetClipboardText`,
  SDL); Ctrl+A selects the whole log. Selecting stops the log from
  scrolling while new lines come.
- The input line: as now (history, Tab completion, Ctrl+V), plus a list of
  suggestions under it as one types, picked with the arrows.
- A filter field: only the lines holding the text (Ctrl+F).
- Scroll: the wheel, a scroll bar, Page Up / Down, Ctrl+Home / End.

## Tasks
- [ ] The panel and the TrueType log, sized for the screen.
- [ ] The mouse: selection, copy, the clipboard.
- [ ] Suggestions and the filter.
- [ ] Both renderers (ref_gl, ref_vk), every resolution from 1280 x 720 to
      3840 x 2160; Half-Life 1.5 and Counter-Strike 1.7.

## Acceptance criteria
- `viewpos`, then its line selected with the mouse and pasted elsewhere
  (outside the game) as it reads.
- The text is sharp at 2560 x 1440.
- Every command and cvar works as before.

## Dependencies
None.

## Risks / notes
- The console is drawn by the engine through the renderers' 2D calls:
  the TrueType glyphs go through the same path as `cl_unifont`'s.
