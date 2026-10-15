# Client HUD additions: `net_graph 4` and Unicode text

## `net_graph 4`

`net_graph` keeps its three levels (1 graph and text, 2 with the data of
each packet, 3 text only). Level 4 is level 3's text with two more lines:

```
120.0 fps                    101/s
in :  20 6.76 kb/s
out:  22 6.95 kb/s
loss: 0 choke: 0
main: 0.4 ms
threads: ■■■■□□□□ 0.3%
```

- `main`: the time the main thread works a frame, averaged over a quarter
  second: input, network, the client library, prediction, recording the
  frame. The renderer's waits for the GPU and the display (fences, image
  acquire, present) are left out: `ref_vk` counts them and tells the engine
  with `PARM_FRAME_WAIT_US`. `ref_gl` doesn't, so there the buffer swap is
  in the number.
- `threads`: a square per worker of the job system
  ([job-system.md](job-system.md)), filled from the bottom by the part of
  the last quarter second it spent in jobs, green to red, then their
  average load. A hollow grey square is a worker the CPU could have but
  that doesn't run (`-jobthreads`, the default of 4).

The block moves up two lines so it stays off the version string;
`net_graphpos` places it as for the other levels. Measuring costs two clock
reads per job and no lock.

Above 200 frames per second, `fps_max` needs `fps_override 1` (the engine's
soft limit in multiplayer); with `gl_vsync 1` the frame rate stops at the
display's refresh rate (`ref_vk` presents in FIFO mode, MAILBOX without
vertical sync).

## Unicode text

The engine's fonts are bitmaps of one codepage (`con_charset`, `cl_charset`
utf-8 with `hud_utf8 1`): a UTF-8 character outside of it, a Japanese player
name for example, used to be drawn as `?` in the HUD, the scoreboard, the
kill feed and the console. Such a character is now drawn from a TrueType
font, with stb_truetype (`3rdparty/stb`), in the cell of the bitmap font:

- each glyph is made once per font height, four times oversampled with
  mipmaps, and made again after a renderer change;
- the fonts tried, loaded only when a character needs them: the file in
  `con_unicode_font` first, then the system's: Meiryo, MS Gothic, Yu Gothic,
  Microsoft YaHei, Malgun Gothic, Segoe UI on Windows; Noto Sans CJK,
  Droid Sans Fallback, DejaVu Sans on Linux; the Android and macOS system
  fonts. The log says which one was loaded (`unicode characters from ...`).

| Variable | Default | Meaning |
|----------|---------|---------|
| `con_unicode_font` | `""` | path of a TrueType font (`.ttf`, `.ttc`, `.otf`) tried before the system ones |

The code is `engine/client/cl_unifont.c`; `CL_DrawCharacter` and
`CL_DrawStringLen` use it when the codepage has no character for the one
decoded.
