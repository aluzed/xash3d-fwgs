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
- [x] Record short demos: HL `c1a0` (lightstyles, water), `c2a5` (sky,
      outdoors), `c3a2` (studio models, sprites, beams); CS `de_dust2`,
      `de_inferno`, `cs_office`. Scripted camera moves, `render_regress.py
      record`. Not covered yet: glass, decals, smoke, flashbang, night
      vision (needs scripted grenades / shots).
- [x] Add a console-driven capture mode: `demo_capture_frames` /
      `demo_capture_dir` cvars, active during `timedemo`; `-timedemo` quits
      at the end. Engine changes were necessary for determinism: fixed
      client time step while capturing, `-fixedseed`, no build number on
      captures.
- [x] Python script (`scripts/render_regress.py`, run with `python3 -I`)
      that launches the engine with `-ref gl|vk`, collects images and produces
      an HTML report with per-image PSNR and diff heatmaps.
- [x] Store reference images outside the repository (they contain Valve
      assets); the script takes their path as argument (`--data`).
- [x] `timedemo` performance report (avg / 1 % low FPS) for both renderers:
      `bench`, with the 1 % / 0.1 % lows and slowest frame the engine now
      prints after `timedemo` (loading frames left out).

## Acceptance criteria
- One command produces the reference set with `ref_gl`.
- Running it twice with `ref_gl` yields identical (or PSNR above threshold)
  images, proving determinism.

## Results (2026-10-14, ref_gl, RTX 2080 Ti, 1280x720)
- Two `ref_gl` capture runs: 30/30 images bit-identical.
- Determinism issues found and fixed: timedemo advanced the client clock by
  the real frame time (entity and light style interpolation), and the
  random tiling texture table plus every later `COM_RandomLong` were seeded
  from the wall clock.
- timedemo average FPS: c1a0 827, c2a5 802, c3a2 1027, de_dust2 711,
  de_inferno 596, cs_office 669.

## Dependencies
001.

## Risks / notes
Particles and random effects are non-deterministic; seed or exclude them, or
compare with a tolerance mask.
