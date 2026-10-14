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
| `r_vk_texture_budget` | `1024` | megabytes of GPU memory for streamed material maps |
| `r_vk_texture_stream` | `1` | stream the mip levels of KTX2 material maps, 0 loads them whole |
| `r_vk_texture_upload` | `512` | megabytes a second of streamed maps sent to the GPU |
| `r_vk_rt` | `0` | build acceleration structures of the scene every frame (ray tracing GPUs, no restart) |
| `r_vk_rt_debug` | `0` | cheat: the scene traced from the camera, 1 albedo, 2 normals, 3 instances, 4 depth against the raster view (red where they differ), 5 albedo with the view model, 6 studio models only, red when online the rays leave them out |
| `r_vk_debug_view` | `""` | cheat: `"x y z pitch yaw"`, the main view drawn from there (regression captures) |

Commands: `r_vk_meminfo` (GPU memory usage), `r_vk_rtinfo` (acceleration
structures of the scene), `texturelist`,
`r_vk_gpu_profile_print` and `r_vk_gpu_profile_reset`. With
`r_vk_gpu_profile 1`, the frame writes a timestamp when it enters each
part (frame start, world, solid entities, effects, translucent, view
model, water, the post-processing stages of `r_vk_quality enhanced`
(SSAO, bloom, exposure, tonemap with antialiasing and sharpening), 2D, present
copy); the profile prints the average and
maximum of each part for the slow frames and for the others, also at
renderer shutdown, e.g. after `-timedemo`. Off, it costs nothing and
doesn't change a pixel; on, it only splits the draw batches at the marks.

## Render graph

`vk_graph.c` keeps, for every image of the frame (main colour and depth,
HDR scene, post-processing images), its last use. A pass declares its
uses (`VK_GraphUse`: colour or depth target, sampled, storage, copy source
or destination) and gets the barrier the two uses need, with their own
stages and accesses, none between two reads in one layout. The transient
images are asked for by name, format and size relative to the main target
(`VK_GraphImage`), made again when the window size changes and freed after
three seconds without use.

## Ray tracing foundation (`r_vk_rt`)

On GPUs with `VK_KHR_acceleration_structure` and `VK_KHR_ray_query` (and
buffer device addresses, 64-bit integers in shaders), `vk_rt.c` keeps the
scene of every frame in a TLAS that shaders trace with ray queries
(`shaders/rt_common.glsl`). Elsewhere, the i386 build with NVIDIA's 32-bit
driver included, the variable does nothing.

- The world: one BLAS with an opaque and an alpha tested geometry (the ray
  query reads the texture's alpha for the second), water in a BLAS of its
  own. Brush models (`*1`, `*2`...) have one each, instanced for the
  brush entities drawn with their transform. All are built with the map
  when ray tracing is on, else at the first frame it is turned on.
- Studio models: the triangles they're drawn with, captured in world
  space, in a BLAS per entity, refit every frame while the triangle count
  stays and rebuilt when it changes, in one build call. Only the models
  drawn are in the scene: rays see what the player can see.
- Sprites, particles, beams and decals are left out, drawn over the
  result as now.
- Instance masks: world, brush entities, models, view model, water,
  translucent.
- Tables of geometries, triangle surfaces and surfaces (texture, lightmap
  page, flags), instances (entity, origin, studio lighting), dynamic
  lights, skybox and light gamma table give what a hit needs to be shaded.

`de_dust2` with 31 bots: the build takes 0.89 ms on average, 1.67 ms at
most (`r_vk_gpu_profile`, "rt build"), for 19 entities and 14 800 studio
triangles. `long_c2a5` drops from about 1740 to 1320 fps with it on.

## Enhanced look (`r_vk_quality`)

`r_vk_quality classic`, the default, draws what `ref_gl` draws. With
`enhanced`, the 3D view goes into an HDR target (16-bit float) and its
colours are linear: everything the game draws is decoded with a gamma 2.2
power, under which the GL blending stays what it was, except that nothing
is clamped any more. The overbright lighting of `gl_overbright` (twice the
texture in gamma space, up to 4.6 times in linear) and additive effects
keep their brightness above white. Compute passes then make the view's
final pixels; the HUD, menus and console are drawn over them as usual.
`rt` adds the ray traced effects below, and renders as `enhanced` on GPUs
without ray queries. Everything changes at once at runtime, no restart.

`r_vk_preset` sets a look's settings at once; the video options menu
offers it as "Rendering quality" while `ref_vk` is loaded, with check
boxes for the four ray traced effects. A setting changed afterwards makes
it `custom`; the preset saved in the configuration isn't applied again at
startup, the settings saved with it are.

| Preset    | What it sets                                                                      |
|-----------|-----------------------------------------------------------------------------------|
| `classic` | `r_vk_quality classic`, the default                                               |
| `low`     | enhanced for integrated GPUs: first bloom level a quarter of the view, no SSAO, FXAA, no sharpening |
| `medium`  | enhanced: half size bloom, SSAO, SMAA, sharpening 0.3                             |
| `high`    | `rt`: medium's post-processing, ray traced shadows (sun 1.5 degrees), occlusion, reflections |
| `ultra`   | high and the ray traced lighting (`r_vk_rt_gi 1`)                                 |

| Variable                   | Default | Meaning                                                        |
|----------------------------|---------|----------------------------------------------------------------|
| `r_vk_quality`             | `classic` | `classic`, `enhanced` or `rt` (enhanced with ray traced effects) |
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
| `r_vk_bloom_start`         | `1`     | first bloom level: 1 half the view, 2 a quarter (lighter)      |
| `r_vk_preset`              | `classic` | `classic`, `low`, `medium`, `high`, `ultra`, `custom`        |

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
- Cost on the RTX 2080 Ti at 1280x720, `long_de_dust2`: 2122 fps classic,
  1161 fps enhanced (1026 before the render graph's barriers), about 0.53 ms of GPU time a frame (SSAO 0.19, bloom
  0.11, exposure 0.02, tonemap with SMAA, sharpening and the copy 0.22;
  0.14 with FXAA).
  Each effect has its own switch for slower GPUs.

## Materials

`.mat` files give a game texture its high resolution and PBR maps (see
`Documentation/hd-textures.md`), with `host_allow_materials 1`. The
classic look takes their albedo only; `enhanced` also uses the normal,
roughness / metalness / occlusion and emissive maps on world surfaces:

- The lightmap pass multiplies the lightmap by the normal map's lighting
  (against the flat surface's) and the occlusion; metals lose their
  diffuse light.
- A material pass then adds, blended additively, the GGX specular
  reflection of the dominant light and the emission.
- The dominant light direction of each lightmap sample is computed at map
  load (`vk_deluxe.c`): direct light of the light entities, the sun
  (`light_environment`, seen when a ray towards it reaches the sky) and
  the texture lights, with rays through the world BSP. 164 ms for
  de_dust2's 168 000 samples. Stored next to the lightmap pages, RGBA8:
  direction in rgb, the share of the light coming from it in alpha (0 for
  light from everywhere, where the normal map changes nothing).
- `r_lightmap 1` shows the lighting with the normal maps.
- KTX2 maps are streamed (`vk_stream.c`): at load only the levels up to
  256 pixels are read; every frame the draws give the screen size of a
  texel of each material, which gives the level each map needs; the
  missing levels are read on a worker thread (only their bytes, straight
  into a staging buffer) and a new image takes them with the levels
  already there (GPU copy), sent at `r_vk_texture_upload` megabytes a
  second so that no frame waits for a whole level. Past
  `r_vk_texture_budget` megabytes (reads in flight counted), the maps
  unseen for two seconds go back to their small levels; when none is, a
  map grows less than it would like, so the maps on screen never take
  turns. Each map switches between two texture slots, so no frame in
  flight sees its image change. `r_vk_texture_stream 0` loads them whole;
  `r_vk_streaminfo [all]` shows where they are.
- Test pack: every de_dust2 texture at 4096 pixels (`scripts/texpack.py`,
  117 KTX2 maps, 1.9 GB on disk). On the `long_de_dust2` demo, enhanced
  look, 1280x720: 1039 fps, 1 % low 553, slowest frame 6.8 ms without the
  pack; 662 fps, 1 % low 244, slowest 7.7 ms with it, under the 1024 MB
  budget (8.0 ms under 512 MB). The i386 build runs it too.
- Each surface's tangent frame is in a storage buffer, read through the
  draw's instance index, and the materials in another (descriptor set 2).

## Ray traced effects (`r_vk_quality rt`)

`vk_rtfx.c` runs compute passes on the HDR scene of the main view, before
the post-processing, with ray queries through the scene of `vk_rt.c`. Each
pixel's surface is found again by a ray from the eye; when the raster
depth is nearer, the pixel shows something left out of the scene (a
sprite, a particle) and stays as drawn.

GoldSrc's light is in the lightmaps, the world's shadows included: the
rays don't light the scene again, they add what the bake couldn't know.

- **Shadows.** Where an entity (a player, a weapon, a door) stands between
  a lightmapped surface and the sun or a dynamic light, the light it
  receives from it leaves its lightmap texel: the sun is movevars'
  `skycolor` times the cosine (the light `light_environment` puts in the
  lightmaps), a dynamic light what `R_AddDynamicLights` adds. The texel is
  decoded and encoded again through `R_BuildLightMap`'s gamma table, so a
  player's shadow is as dark as the baked shadow of a wall next to it.
  The sun counts only where the world itself lets it through, where the
  lightmap has it. Studio models lit by the sky (`R_EntityDynamicLight`)
  lose their directional light where the world or another entity hides
  the sun. A dynamic light ignores the entity it follows (the muzzle
  flash of a player doesn't make the player's shadow). The sun is a disc
  (`r_vk_rt_sun_size`): four rays a pixel towards points of it, kept 4
  frames and averaged over 3x3 pixels, soft shadow edges without trails
  behind running players; dynamic lights cast hard shadows.
- **Ambient occlusion**, in place of SSAO: one ray a pixel in the normal's
  hemisphere, accumulated over up to 24 frames where the surface stays
  (reprojected with the previous view, rejected where its distance to the
  eye changed: 4 frames on studio models), then a 5x5 filter over the
  pixels at the same distance.
- **Reflections** on water, glass (brush entities drawn with
  `kRenderTransTexture`, translucent `func_water`) and materials with
  `reflective 1`: a mirror ray, a little rippled on water, with Schlick's
  Fresnel; what it hits is lit as the raster draws it (texture times
  lightmap, or the studio model's lit vertex colour), the skybox when it
  hits nothing.

| Variable              | Default | Meaning                                                         |
|-----------------------|---------|-----------------------------------------------------------------|
| `r_vk_rt_shadows`     | `1`     | shadows of entities in the sun and the dynamic lights           |
| `r_vk_rt_ao`          | `1`     | ray traced ambient occlusion strength, 0 off                    |
| `r_vk_rt_ao_radius`   | `32`    | its radius in world units                                       |
| `r_vk_rt_reflections` | `1`     | reflections strength, 0 off                                     |
| `r_vk_rt_sun`         | `1`     | part of `skycolor` the sun put in the lightmaps                 |
| `r_vk_rt_shadow_keep` | `0.3`   | least part of its light a shadowed surface keeps                |
| `r_vk_rt_sun_size`    | `1.5`   | angular size of the sun in degrees, 0 for hard shadows          |

`r_vk_gpu_profile` counts the lighting below as "rt lighting", the rest
as "rt effects".

**Online, the rays show no more than the player sees.** A studio model is
in the scene only when drawn, and a pass marks those whose points (feet,
middle, head, sides) the eye can't see past the world and the brush
entities: they cast no shadow, no occlusion and show in no reflection
(`r_vk_rt_debug 6` shows them red). A player around a corner isn't given
away by a shadow on the floor in view. The exposure limit of
the enhanced look applies too.

**A server can keep its players from it.** The physinfo key `maxlook`
(`classic` or `enhanced`) caps the look of every `ref_vk` client: the game
or a plugin sets it with `SetPhysicsKeyValue` on HLDS / ReHLDS, a Xash
server with `sv_maxlook`. It goes through the physinfo string the server
already sends: no protocol change.

On an RTX 2080 Ti at 1280x720, the passes take 0.6-0.7 ms
(`r_vk_gpu_profile`, "rt effects"), the acceleration structures 0.2 ms
alone, 1.6 ms with 31 bots in view. The long demos (3 runs):

| Demo            | enhanced avg / 1 % / 0.1 % low | rt avg / 1 % / 0.1 % low |
|-----------------|--------------------------------|--------------------------|
| `long_c2a5`     | 1119 / 557 / 237 fps           | 606 / 388 / 282 fps      |
| `long_de_dust2` | 1207 / 579 / 208 fps           | 609 / 366 / 139 fps      |

The map's acceleration structures are built with the map while loading
(it waits for them): built at its first frame, they made it 17 ms.

### Ray traced lighting (`r_vk_rt_gi`)

With `r_vk_rt_gi` above 0, the rays light the lightmapped surfaces too,
from the lights the map was compiled with (`vk_rtlights.c`, extracted
with the map's acceleration structures):

- `light` and `light_spot` entities (colour, brightness, cones, the
  lightstyle the compiler gave switchable ones), `~` texture lights (their
  texture's average colour; `lights.rad` doesn't ship with the games, so
  they all get brightness 300), emissive material maps, the sun of the
  movevars, and the dynamic lights with `R_AddDynamicLights`' falloff.
- A grid of 256 unit cells lists the lights reaching each cell. Per pixel:
  one light without a style picked by its light (resampled importance
  sampling) with a shadow ray, and one bounce whose hit is lit by its
  lightmap (the bake's light, bounces included) or by the sky; that part
  is accumulated over the frames and filtered by depth and normal. The
  sun and the lights with a style are traced every frame, so lightstyles
  animate; the history starts over where a lightmap changed (a light
  switched).
- The result replaces `r_vk_rt_gi` of the lightmap's light, through the
  same gamma table as the shadows; where the lightmap is nearly black,
  the colour comes from the texture. Studio models keep GoldSrc's
  lighting.

| Variable            | Default | Meaning                                                    |
|---------------------|---------|------------------------------------------------------------|
| `r_vk_rt_gi`        | `0`     | part of the lighting the rays give, 1 the rays only        |
| `r_vk_rt_gi_scale`  | `1`     | their brightness against the lightmaps                     |
| `r_vk_rt_gi_bounce` | `1`     | strength of the bounce, 0 skips its ray                    |
| `r_vk_rt_gi_sky`    | `0.25`  | light of the sky around, as a part of `skycolor`           |

`r_vk_rtlights [count]` lists the lights nearest the view. A map can be
tuned with `maps/<map>.rtlights` (`scale`, `texlight <texture> r g b
brightness`, `light x y z r g b brightness [style]`). The lighting alone
(`r_lightmap 1`) is as bright as the lightmaps': 68 against 62 in a lab of
c1a0, 75 against 72 at the T spawn of de_dust2 (mean of the view). It
costs 1-2.5 ms at 1280x720: `long_c2a5` 379 fps, `long_de_dust2` 366.

### Denoising and motion

The effects with noise (shadows, occlusion, lighting) keep a history of
their own and filter it, by depth (and normal for the lighting). A
pixel's history is the previous frame's pixel that saw the same point
where it was then: the motion of the world is the camera's, brush
entities moved with their transform of the previous frame, studio
models with their previous frame's triangles (the same triangle of the
same skinning, copied by the GPU for the next frame). A history is only
used when that pixel was as far from the previous eye as the point,
and the lighting's starts over where a lightmap changed. Reflections
are mirrors: no noise to filter.

No temporal upscaling: at 2560x1440 the `high` preset runs `long_de_dust2`
at 315 fps (1 % low 196), `ultra` at 180 (1 % low 99), and GoldSrc's
crisp, low detail look and online play lose more to a temporal
reconstruction's blur and lag than they would gain. FSR 2/3 (MIT) could
come later for weak GPUs; DLSS's SDK licence doesn't go with the GPL.

Sprites and additive effects drawn over a shadowed surface are darkened
with it.

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
