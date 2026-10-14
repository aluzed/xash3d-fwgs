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
  (`3rdparty/Vulkan-Headers`), SMAA as well (`3rdparty/smaa`); shaders are
  compiled with `glslc`
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
model, water, the post-processing stages of `r_vk_quality enhanced`
(SSAO, bloom, exposure, tonemap with antialiasing and sharpening), 2D, present
copy); the profile prints the average and
maximum of each part for the slow frames and for the others, also at
renderer shutdown, e.g. after `-timedemo`. Off, it costs nothing and
doesn't change a pixel; on, it only splits the draw batches at the marks.

## Enhanced look (`r_vk_quality`)

`r_vk_quality classic`, the default, draws what `ref_gl` draws. With
`enhanced`, the 3D view goes into an HDR target (16-bit float) and its
colours are linear: everything the game draws is decoded with a gamma 2.2
power, under which the GL blending stays what it was, except that nothing
is clamped any more. The overbright lighting of `gl_overbright` (twice the
texture in gamma space, up to 4.6 times in linear) and additive effects
keep their brightness above white. Compute passes then make the view's
final pixels; the HUD, menus and console are drawn over them as usual.
`rt` is reserved for the ray tracing stories and renders as `enhanced` for
now. Everything changes at once at runtime, no restart. The video options
menu has the choice ("Rendering quality", classic or enhanced) while
`ref_vk` is loaded.

| Variable                   | Default | Meaning                                                        |
|----------------------------|---------|----------------------------------------------------------------|
| `r_vk_quality`             | `classic` | `classic`, `enhanced` or `rt`                                |
| `r_vk_tonemap`             | `3`     | 0 clamp, 1 ACES (Hill's fit), 2 AgX, 3 neutral                 |
| `r_vk_exposure`            | `0`     | exposure compensation, in EV                                   |
| `r_vk_autoexposure`        | `1`     | adapt the exposure to the view like an eye                     |
| `r_vk_autoexposure_range`  | `1`     | how far it may go, in EV either way                            |
| `r_vk_autoexposure_speed`  | `1.5`   | how fast it adapts                                             |
| `r_vk_bloom`               | `0.5`   | bloom intensity, 0 off                                         |
| `r_vk_bloom_threshold`     | `1`     | linear brightness above which pixels bloom                     |
| `r_vk_ssao`                | `1`     | ambient occlusion strength, 0 off                              |
| `r_vk_ssao_radius`         | `24`    | ambient occlusion radius in world units                        |
| `r_vk_aa`                  | `2`     | antialiasing: 0 none, 1 FXAA, 2 SMAA                           |
| `r_vk_sharpen`             | `0.3`   | contrast adaptive sharpening, 0 off                            |

- Tonemappers: "neutral" is the highlight compression of Khronos PBR
  Neutral without its toe, so colours below 0.76 are exactly the classic
  ones; only what the classic look clamps is compressed. ACES and AgX get
  an exposure offset (+1.15 and -0.25 EV) that keeps the dark middle tones
  of HL and CS (log average luminance around 0.05) where the classic look
  has them. 0 clamps like the classic look: with every effect off, the
  only differences left are the linear blending and the 8-bit rounding.
- The automatic exposure brings the log average luminance of the view to
  0.05, which is where most HL and CS views are, within
  `r_vk_autoexposure_range`. In multiplayer (more than one client slot)
  the exposure never goes above the classic one: it may darken a bright
  view, never brighten a dark corner.
- Bloom: 13-tap downsamples of what is over the threshold (soft knee) to
  a few pixels, tent filtered upsamples added level by level.
- SSAO works from the depth alone, for GPUs without ray tracing, at half
  size: 12 samples in the normal's hemisphere, a 4x4 depth aware blur,
  bilinear upsampling. The view model is drawn with a compressed depth
  range, so its occlusion is approximate.
- Antialiasing: FXAA is the quality preset of FXAA 3.11, one pass that
  blends across the edges it finds, slightly blurring fine texture
  detail. SMAA 1x is the reference implementation of Jimenez et al.
  (high preset, colour edges) in three passes: edges, blending weights
  from the edge shapes (precomputed area and search textures), blending.
  It only touches real edges (0.3 % of the pixels of a de_dust2 view
  against 2.7 % for FXAA) and costs some 0.075 ms more.
- Cost on the RTX 2080 Ti at 1280x720, `long_de_dust2`: 2175 fps classic,
  1026 fps enhanced, about 0.53 ms of GPU time a frame (SSAO 0.19, bloom
  0.11, exposure 0.02, tonemap with SMAA, sharpening and the copy 0.22;
  0.14 with FXAA).
  Each effect has its own switch for slower GPUs.

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
- `r_vk_quality enhanced` doesn't apply to custom renderers that replace
  the whole frame (client `GL_RenderFrame`), nor to `envshot` / `skyshot`
  and the overview, which stay classic.
- Bullet holes and sprays on alpha tested brush entities (grates) didn't
  show in the harness scene with either renderer; the decal code is
  `ref_gl`'s, but this case has no capture.
