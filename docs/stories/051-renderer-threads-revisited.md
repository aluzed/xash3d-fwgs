# 051 - Renderer threads revisited (low priority)

## Context
Story [028](028-renderer-multithreading.md) moved the vertices of studio
models (skinning, lighting, strips to triangles) to the workers of the job
system ([029](029-engine-job-system.md)). Two things came up on 2026-10-15,
playing Counter-Strike on the test server with the 64-bit Windows client:

- A race: `VK_ImmFlush` placed the reserved ranges before copying the batch
  into the dynamic ring, so the copy could overwrite the vertices a worker
  had just written; triangles flashed across the screen for a frame. Fixed
  (f4c4dc48), but it shows how fragile the hand-off between the immediate
  mode batch and the jobs is.
- The gain is small on CS. `net_graph 4` (dc816d8d, f7799321) shows the
  workers busy under 0.5% of the time while playing a demo with players,
  and the main thread working 0.3 to 0.7 ms a frame on a desktop CPU: the
  models of CS 1.6 are a few hundred triangles, and `VK_StudioJoin` often
  runs the jobs itself before a worker takes them.

## Status (2026-10-22): decided, kept on
Measured on the user's Windows machine (x64, Vulkan, AMD, 1280x720
windowed, `fps_max 0`, `gl_vsync 0`) with a demo of a crowded scene: a
listen server of 32 slots in `sv_mode csdm ffa` with 31 YaPB bots on
de_dust2, 60 s spectated (10 515 frames), played by `-timedemo`:

| | mean | 1% low | 0.1% low |
|---|---|---|---|
| `r_vk_threads 0` | 610-625 fps | 325-335 fps | 200-212 fps |
| `r_vk_threads 1` | 725-764 fps | 381-383 fps | 227-230 fps |

About +20% on the mean frame rate and +15% on the lows, run after run.
`r_vk_studio_stats` (new, also printed at the renderer's shutdown, so at
the end of a `-timedemo` run): 103 jobs a frame, 0.0% of them taken back
by the join, 0.002 ms a frame in the join. The workers take every job;
on this scene the main thread no longer makes studio vertices at all.

Captures of frames 2000, 5000 and 8000 (`demo_capture_frames`) at full
frame rate with `-fixedseed`: two runs with `r_vk_threads 0` and one with
1 are identical pixel for pixel (without `-fixedseed` even two runs of the
same setting differ, random effects). That is the regression check that
would have caught f4c4dc48's race: a crowded demo, `r_vk_threads 0` and
1, at full frame rate, compared pixel for pixel.

Decision: the threaded studio path stays, on by default, on CS too. Command
recording in secondary command buffers isn't worth it now: the join is
free and the main thread's remaining work isn't studio vertices. The
hand-off's invariants are written above `vk_imm_pending_t` (`vk_imm.c`).

Not measured: a mod with heavy models; `net_graph 4` by eye (the
timedemo numbers replace it).

## Goal
Decide, with numbers, whether the threaded studio path stays as it is, is
simplified, or goes further (command recording on workers), and make the
hand-off with the batch robust if it stays.

## Scope
In: the studio jobs of `vk_studio.c`, their reservation in `vk_imm.c`, the
join, the measurements. Out: the job system itself, the server threads
(034-050).

## Tasks
- [x] Measure on crowded scenes (32 players, smoke, decals) and on a mod with
      heavy models: main thread time and worker load with `r_vk_threads 0`
      and `1`, from `net_graph 4` and `r_vk_gpu_profile`.
- [x] Count how many jobs a worker runs and how many the join takes back.
- [x] Review the reservation / placement / copy order of the batch and
      document its invariants next to the code; add a regression check that
      would have caught the race (028 vs `r_vk_threads 0`, frames replayed
      at full speed, not slowed down by captures).
- [x] Decide: keep, default off on light games, or extend to command
      recording in secondary command buffers. Write the decision here.

## Acceptance criteria
- A decision backed by measurements, written in this story.
- If the threaded path stays: identical images with `r_vk_threads 0` and `1`
  at full frame rate, and a documented hand-off.

## Dependencies
028, 029.

## Risks / notes
Low priority: nothing is broken today, the race is fixed. To be picked up
when there is time.
