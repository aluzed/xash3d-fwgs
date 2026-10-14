# 009 - Texture manager

## Context
The engine and game DLLs address textures by integer indices returned by
`GL_LoadTexture`, `GL_FindTexture`, `GL_CreateTexture*` etc. Client DLLs
(HUD, VGUI) depend on these indices and on `R_GetTextureParms`.

## Goal
All texture entry points of `ref_interface_t` implemented on Vulkan with the
same observable semantics as `ref_gl`.

## Tasks
- [x] Texture table with the same index allocation rules and built-in
      textures as `ref_gl` (`*white`, `*black`, `*particle`, default
      checkerboard, etc.).
- [x] Upload paths for all `rgbdata_t` formats the image library produces
      (8-bit palettised already expanded, RGBA, compressed DXT if present),
      mipmap generation on GPU (blit chain), cubemaps, arrays. Mipmaps are
      built on the CPU with ref_gl's exact filter (and its 1-bit alpha box
      filter) instead of a blit chain, for bit-exact parity; the GL
      internal format is emulated with a view swizzle (RGB samples alpha
      as 1, luminance replicates red). Float, depth and multisample
      textures are refused for now.
- [x] Texture flags: nearest / linear, clamp, `TF_NOMIPMAP`, `TF_KEEP_SOURCE`,
      luma, alpha-test (`{` textures), `gl_texturemode`, anisotropy,
      `gl_round_down` / `gl_max_size` equivalents.
- [x] Texture updates (`GL_UpdateTexture`, VGUI dynamic textures, sprays,
      `R_UpdateRawTexture`-style paths).
- [x] Deferred destruction (textures in use by in-flight frames).
- [x] `texturelist` console command.

## Acceptance criteria
- Texture indices returned for a given load sequence match `ref_gl`.
- Menus and HUD load all their images (checked with `texturelist`).

## Results (2026-10-14)
- Menus, console and HUD load all their images; the 2D captures match
  ref_gl bit for bit (story 010), which also validates sampling, filtering
  and the channel swizzle.
- Texture numbers differ from ref_gl while the world textures (story 012)
  and studio textures (story 017) aren't loaded by ref_vk yet.

## Dependencies
007, 008.
