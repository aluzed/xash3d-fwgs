# 009 - Texture manager

## Context
The engine and game DLLs address textures by integer indices returned by
`GL_LoadTexture`, `GL_FindTexture`, `GL_CreateTexture*` etc. Client DLLs
(HUD, VGUI) depend on these indices and on `R_GetTextureParms`.

## Goal
All texture entry points of `ref_interface_t` implemented on Vulkan with the
same observable semantics as `ref_gl`.

## Tasks
- [ ] Texture table with the same index allocation rules and built-in
      textures as `ref_gl` (`*white`, `*black`, `*particle`, default
      checkerboard, etc.).
- [ ] Upload paths for all `rgbdata_t` formats the image library produces
      (8-bit palettised already expanded, RGBA, compressed DXT if present),
      mipmap generation on GPU (blit chain), cubemaps, arrays.
- [ ] Texture flags: nearest / linear, clamp, `TF_NOMIPMAP`, `TF_KEEP_SOURCE`,
      luma, alpha-test (`{` textures), `gl_texturemode`, anisotropy,
      `gl_round_down` / `gl_max_size` equivalents.
- [ ] Texture updates (`GL_UpdateTexture`, VGUI dynamic textures, sprays,
      `R_UpdateRawTexture`-style paths).
- [ ] Deferred destruction (textures in use by in-flight frames).
- [ ] `texturelist` console command.

## Acceptance criteria
- Texture indices returned for a given load sequence match `ref_gl`.
- Menus and HUD load all their images (checked with `texturelist`).

## Dependencies
007, 008.
