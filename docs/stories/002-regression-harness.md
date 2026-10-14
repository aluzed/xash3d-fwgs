# 002 - Visual regression harness

## Context
Parity with `ref_gl` must be measurable. Xash3D supports demos
(`record` / `playdemo` / `timedemo`) and screenshots.

## Goal
A script that renders a fixed set of frames with any renderer and diffs them
against `ref_gl` reference images.

## Scope
In: capture script, reference set, image diff report, timedemo benchmark.
Out: CI integration on GitHub (local only for now).

## Tasks
- [ ] Record short demos: HL `c1a0` (lightstyles, water), `c2a5` (sky,
      outdoors), `c3a2` (studio models, sprites, beams); CS `de_dust2`,
      `de_inferno`, `cs_office` (glass, decals, smoke, flashbang, night vision).
- [ ] Add a console-driven capture mode: play a demo, pause on listed frame
      numbers, take `screenshot` (or extend `VID_ScreenShot` usage), quit.
      Prefer existing commands/cvars; add engine changes only if necessary.
- [ ] Python script (`scripts/render_regress.py`, run with `python3 -I`)
      that launches the engine with `-ref gl|vk`, collects images and produces
      an HTML report with per-image PSNR and diff heatmaps.
- [ ] Store reference images outside the repository (they contain Valve
      assets); the script takes their path as argument.
- [ ] `timedemo` performance report (avg / 1 % low FPS) for both renderers.

## Acceptance criteria
- One command produces the reference set with `ref_gl`.
- Running it twice with `ref_gl` yields identical (or PSNR above threshold)
  images, proving determinism.

## Dependencies
001.

## Risks / notes
Particles and random effects are non-deterministic; seed or exclude them, or
compare with a tolerance mask.
