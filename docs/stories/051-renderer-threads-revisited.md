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

## Goal
Decide, with numbers, whether the threaded studio path stays as it is, is
simplified, or goes further (command recording on workers), and make the
hand-off with the batch robust if it stays.

## Scope
In: the studio jobs of `vk_studio.c`, their reservation in `vk_imm.c`, the
join, the measurements. Out: the job system itself, the server threads
(034-050).

## Tasks
- [ ] Measure on crowded scenes (32 players, smoke, decals) and on a mod with
      heavy models: main thread time and worker load with `r_vk_threads 0`
      and `1`, from `net_graph 4` and `r_vk_gpu_profile`.
- [ ] Count how many jobs a worker runs and how many the join takes back.
- [ ] Review the reservation / placement / copy order of the batch and
      document its invariants next to the code; add a regression check that
      would have caught the race (028 vs `r_vk_threads 0`, frames replayed
      at full speed, not slowed down by captures).
- [ ] Decide: keep, default off on light games, or extend to command
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
