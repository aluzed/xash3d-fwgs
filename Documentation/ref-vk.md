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

Commands: `r_vk_meminfo` (GPU memory usage), `texturelist`.

## Performance

`timedemo` on an RTX 2080 Ti (driver 580, 1280x720, same demos for both
renderers): the three Half-Life demos run 15-18 % faster than with
`ref_gl`, the three Counter-Strike demos 48-57 % faster.

## Differences with ref_gl and known issues

- Detail textures (`*_detail.txt`) aren't drawn. Half-Life and
  Counter-Strike ship none; under fog the stub pass `ref_gl` adds is drawn.
- `r_ripple` water simulation isn't supported (off by default in `ref_gl`).
- Quake-only features: alias models, the Quake sky sphere and clouds.
- Studio skinning and lighting run on the CPU like `ref_gl`.
- `r_shadows` draws without `ref_gl`'s stencil test, so overlapping shadow
  triangles may darken twice.
- `VID_CubemapShot` (`envshot`, `skyshot`) isn't implemented.
- Not checked against `ref_gl` by an automatic capture yet: translucent
  world water (`sv_wateralpha`), `func_water` entities, player sprays,
  decals on transparent surfaces.
