# 010 - 2D drawing: menu, console, HUD

## Goal
Main menu (mainui / GameUI), console, HUD, VGUI and client-drawn 2D look
identical to `ref_gl`.

## Tasks
- [ ] `R_DrawStretchPic`, `R_DrawTileClear`, `FillRGBA`, `GL_SetRenderMode`,
      `Color4ub`, scissor (`GL_Scissor`/`R_SetScissor` style), ortho setup.
- [ ] Render modes: normal, transColor, transTexture, glow, transAlpha,
      transAdd; exact GoldSrc blend equations.
- [ ] Batched quads in the per-frame ring, one draw per state change.
- [ ] VGUI1 (CS scoreboard, team menu, MOTD) and VGUI2/GameUI paths.
- [ ] Safe area and console font rendering paths used by the engine.

## Acceptance criteria
- Regression harness: menu, console, HUD in HL and CS (buy menu, scoreboard,
  radar, crosshair) match `ref_gl` above PSNR threshold.

## Dependencies
009, 006.
