# 023 - Texture upscaling toolchain (4K packs)

## Goal
Offline tooling that extracts every texture used by HL / CS, upscales it with
an ML model, derives PBR maps, and outputs a `materials/` pack (022 layout).

## Tasks
- [ ] Extractor (`utils/` or `scripts/`): WAD3 textures, BSP embedded
      textures, studio model skins, sprites; keeps palette / `{` alpha mask
      / luma info; outputs PNG + JSON manifest.
- [ ] Upscale stage, model-agnostic (CLI wrapper around the chosen model,
      e.g. Real-ESRGAN class models); tiling with seamless wrap for
      repeating world textures.
- [ ] Alpha-mask textures: upscale mask separately and re-threshold.
- [ ] PBR derivation stage (normal / roughness from albedo via model or
      heuristics), manually overridable per texture.
- [ ] Compression stage to KTX2 BC formats with mipmaps.
- [ ] Incremental: only reprocess changed inputs; manifest with hashes.

## Progress (2026-10-14)
- [~] Compression stage, pulled forward for 022's test pack:
      `utils/ktx2enc` (built with `--enable-utils`) turns a PNG into a
      KTX2 with every mip level, BC7 (colour, linear data) or BC5 (normal
      maps) or BC4, with bc7enc / rgbcx (`3rdparty/bc7enc_rdo` submodule,
      MIT). Colour levels are averaged in linear light, normal map levels
      renormalized. Not incremental yet, no manifest.

## Acceptance criteria
- One command produces a pack for `cstrike` and one for `valve` from the
  Steam install.

## Risks / notes
- Packs derived from Valve assets are for personal use only; never commit
  them to the repository or redistribute them.
- Run tooling on extracted data from scripts outside the data directory.

## Dependencies
022 (layout frozen).
