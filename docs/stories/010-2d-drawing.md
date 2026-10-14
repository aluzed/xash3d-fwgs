# 010 - 2D drawing: menu, console, HUD

## Goal
Main menu (mainui / GameUI), console, HUD, VGUI and client-drawn 2D look
identical to `ref_gl`.

## Tasks
- [x] `R_DrawStretchPic`, `R_DrawTileClear`, `FillRGBA`, `GL_SetRenderMode`,
      `Color4ub`, scissor (`GL_Scissor`/`R_SetScissor` style), ortho setup.
      `vk_imm.c` emulates the GL 1.x state these calls rely on (current
      colour, bound texture, blend, alpha test, clear colour), persistent
      across frames like GL. A negative viewport height keeps GL's Y axis
      so ref_common's GL matrices are used unchanged. No scissor call
      exists in the current interface.
- [x] Render modes: normal, transColor, transTexture, glow, transAlpha,
      transAdd; exact GoldSrc blend equations.
- [x] Batched quads in the per-frame ring, one draw per state change.
- [ ] VGUI1 (CS scoreboard, team menu, MOTD) and VGUI2/GameUI paths.
      `VGUI_SetupDrawing` is done; cs16-client draws its scoreboard and
      menus through mainui and TriAPI (covered); VGUI1 itself isn't
      exercised by the 64-bit game libraries.
- [x] Safe area and console font rendering paths used by the engine.

## Acceptance criteria
- Regression harness: menu, console, HUD in HL and CS (buy menu, scoreboard,
  radar, crosshair) match `ref_gl` above PSNR threshold.

## Results (2026-10-14)
Regression harness, static scenes (`-fixedfps 60 -fixedseed`):

| Capture | ref_vk vs ref_gl |
|---|---|
| HL console | identical |
| HL HUD (`r_norefresh 1`) | identical |
| CS HUD, scoreboard, buy menu | identical |
| CS main menu | 87.8 dB |
| HL main menu (animated banner masked) | 73.9 dB |

The HL menu banner animates with libc `rand()`, also consumed by other
code, so it differs between two ref_gl runs too: `compare` masks it.
`-vkvalidation`: 0 messages over all captures.

## Dependencies
009, 006.
