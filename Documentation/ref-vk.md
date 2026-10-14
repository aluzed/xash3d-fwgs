# ref_vk: the Vulkan renderer

`ref_vk` is a Vulkan renderer for Half-Life and Counter-Strike 1.6. Its
first goal is to draw exactly what `ref_gl` draws: most of it is `ref_gl`'s
code (BSP surfaces, studio models, sprites, beams, particles, decals, fog)
with the OpenGL calls replaced, and a regression harness compares both
renderers frame by frame. Network protocol, game libraries and simulation
are untouched: a client using `ref_vk` plays on any server.

## Requirements

- A Vulkan 1.3 device with dynamic rendering, synchronization2, descriptor
  indexing (partially bound, update after bind), buffer device address.
  BC texture compression is used when present. Developed and tested on
  NVIDIA (RTX 2080 Ti); other vendors are untested so far.
- SDL2 with Vulkan support for the window.
- To build: the Vulkan headers come as a git submodule
  (`3rdparty/Vulkan-Headers`); shaders are compiled with `glslc`
  (package `glslc` or the Vulkan SDK).

## Building

```
./waf configure --enable-vk ...
./waf build install
```

The renderer is installed as `libref_vk.so` next to `libref_gl.so`. See
`docs/building-vulkan-renderer.md` for the development setup and the
regression harness.

## Selecting it

- In the video options menu: renderer "Vulkan" (restart the game).
- On the command line: `-ref vk`.
- In `video.cfg`: `r_refdll "vk"`.

OpenGL stays the default. When Vulkan can't initialize (no driver, no
suitable device), the engine falls back to the next renderer, OpenGL, and
remembers it. A renderer asked for with `-ref` warns about the failure
before falling back; one chosen in the menu falls back silently.

## Console variables

Shared with `ref_gl`, same meaning and defaults (saved to `vulkan.cfg`):
`gl_overbright`, `gl_anisotropy`, `gl_texture_lodbias`, `gl_texture_nearest`,
`gl_lightmap_nearest`, `gl_keeptjunctions`, `gl_litwater_force`, `gl_fog`,
`gl_polyoffset`, `gl_polyoffset_bmodels`, `r_large_lightmaps`,
`r_detailtextures`, `r_traceglow`, `r_studio_sort_textures`,
`r_studio_drawelements`, `r_studio_builtin_renderer`, `r_shadows`, `r_novis`,
`r_nocull`, `r_lockpvs`, `r_lockfrustum`, and the engine ones (`gl_vsync`,
`r_dynamic`, `r_fullbright`, `r_drawentities`...).

Vulkan specific:

| Variable          | Default | Meaning                                                        |
|-------------------|---------|----------------------------------------------------------------|
| `r_vk_device`     | `-1`    | physical device index, `-1` picks the best one (restart)       |
| `r_vk_validation` | `0`     | enable the validation layer (restart); `-vkvalidation` as well |
| `r_vk_dynamic_mb` | `8`     | size of each per-frame dynamic vertex ring, grows on overflow  |
| `r_vk_log_stubs`  | `0`     | log every unimplemented call once                              |
| `r_vk_gpu_profile` | `0`    | GPU time of each part of the frame (timestamps)                |
| `r_vk_gpu_profile_slow` | `0.35` | GPU frame time (ms) above which the profile counts a frame as slow |

Commands: `r_vk_meminfo` (GPU memory usage), `texturelist`,
`r_vk_gpu_profile_print` and `r_vk_gpu_profile_reset`. With
`r_vk_gpu_profile 1`, the frame writes a timestamp when it enters each
part (frame start, world, solid entities, effects, translucent, view
model, water, 2D, present copy); the profile prints the average and
maximum of each part for the slow frames and for the others, also at
renderer shutdown, e.g. after `-timedemo`. Off, it costs nothing and
doesn't change a pixel; on, it only splits the draw batches at the marks.

## Performance

`timedemo` on an RTX 2080 Ti (driver 580, 1280x720, same demos for both
renderers): the three Half-Life demos run 15-18 % faster than with
`ref_gl`, the three Counter-Strike demos 48-57 % faster.

`timedemo` also prints the 1 % and 0.1 % lows (average frame rate of the
slowest frames) and the slowest frame, leaving out the frames that start
or end while the client isn't active: the loading at the start of a demo
and the level changes inside it (the 30-250 ms frames, in
`svc_resourcelist` checking the resources and the first `svc_clientdata`).
On the long harness demos (`long_c2a5`, about 2500 frames with five level
changes, `long_de_dust2`, 3 runs):

| Demo            | ref_gl avg / 1 % / 0.1 % low | ref_vk avg / 1 % / 0.1 % low |
|-----------------|------------------------------|------------------------------|
| `long_c2a5`     | 1567 / 541 / 196 fps         | 1814 / 707 / 275 fps         |
| `long_de_dust2` | 1736 / 851 / 220 fps         | 2159 / 726 / 220 fps         |

No frame of play takes more than 6.5 ms with either renderer. On
de_dust2 the 1 % low of ref_vk is below ref_gl's although its average is
higher. `r_vk_gpu_profile` shows where: about one frame in five, the copy
of the frame into the swapchain image takes 0.3-1.2 ms instead of 0.04 ms,
every ~2 ms whatever the view. The copy waits for the image the window
system gives back: X11 takes the presented images at its own pace, under
the 2000 frames per second of a timedemo. The same with no other program
on the GPU, in borderless and fullscreen windows, with 3-5 frames in
flight, 2 more swapchain images, IMMEDIATE or MAILBOX. It only shows above
some 1500 frames per second; the 1 % low of 650-730 fps it leaves is
1.5 ms frames.

## Differences with ref_gl and known issues

- Detail textures (`*_detail.txt`) aren't drawn. Half-Life and
  Counter-Strike ship none; under fog the stub pass `ref_gl` adds is drawn.
- `r_ripple` water simulation isn't supported (off by default in `ref_gl`).
- Quake-only features: alias models, the Quake sky sphere and clouds.
- Studio skinning and lighting run on the CPU like `ref_gl`.
- Bullet holes and sprays on alpha tested brush entities (grates) didn't
  show in the harness scene with either renderer; the decal code is
  `ref_gl`'s, but this case has no capture.
