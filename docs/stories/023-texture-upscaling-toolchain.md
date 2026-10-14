# 023 - Texture upscaling toolchain (4K packs)

## Goal
Offline tooling that extracts every texture used by HL / CS, upscales it with
an ML model, derives PBR maps, and outputs a `materials/` pack (022 layout).

## Tasks
- [x] Extractor (`utils/` or `scripts/`): WAD3 textures, BSP embedded
      textures, studio model skins, sprites; keeps palette / `{` alpha mask
      / luma info; outputs PNG + JSON manifest.
- [x] Upscale stage, model-agnostic (CLI wrapper around the chosen model,
      e.g. Real-ESRGAN class models); tiling with seamless wrap for
      repeating world textures.
- [x] Alpha-mask textures: upscale mask separately and re-threshold.
- [x] PBR derivation stage (normal / roughness from albedo via model or
      heuristics), manually overridable per texture.
- [x] Compression stage to KTX2 BC formats with mipmaps.
- [x] Incremental: only reprocess changed inputs; manifest with hashes.

## Progress (2026-10-14)
- [x] Compression stage, pulled forward for 022's test pack:
      `utils/ktx2enc` (built with `--enable-utils`) turns a PNG into a
      KTX2 with every mip level, BC7 (colour, linear data) or BC5 (normal
      maps) or BC4, with bc7enc / rgbcx (`3rdparty/bc7enc_rdo` submodule,
      MIT). Colour levels are averaged in linear light, normal map levels
      renormalized.
- [x] `scripts/texpack.py`: world textures of given maps or of every map
      (`--all-maps`, shared ones once in `materials/common`), model skins
      (`--models`, `*T.mdl` included, player colour textures left out),
      sprite frames (`--sprites`, TGA the engine loads); Lanczos or any
      upscaler command; world textures upscaled with a wrapped border so
      their edges meet; transparent pixels filled from their neighbours and
      alpha masks upscaled apart then thresholded; derived normal and rma
      maps, replaced by hand made ones from `--overrides`, hand edited
      `.mat` files kept; ktx2enc compression; manifest of hashes. One
      command per game: `--all-maps --models --sprites`.
- Not done: running an ML upscaler (any can be plugged with `--upscaler`,
  none is bundled), sprite groups (single frames only).

## Acceptance criteria
- One command produces a pack for `cstrike` and one for `valve` from the
  Steam install.

## Risks / notes
- Packs derived from Valve assets are for personal use only; never commit
  them to the repository or redistribute them.
- Run tooling on extracted data from scripts outside the data directory.

## Dependencies
022 (layout frozen).
