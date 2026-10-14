# Job system: what runs on worker threads, and what never will

`engine/common/jobs.c` runs jobs on a fixed pool of worker threads
(`-jobthreads N`, default the CPU count minus one, up to 4; `-jobthreads 0`
runs every job on the spot). A job is a function that only touches its own
data; its `done` function then runs on the main thread, at the start of
`Host_Frame` or in `Job_WaitIdle`, and is the only place where the result
meets the rest of the engine. Renderers get `Job_Submit` and
`Job_WaitIdle` through `ref_api_t` (`REF_API_VERSION` 22).

Checked with ThreadSanitizer: `Test_RunJobs` in the engine tests
(`--enable-tests`, `-runtests`) runs 2000 jobs, done functions that submit
more and a full queue, without a report. The client itself can't run
under TSan with the NVIDIA driver loaded (TSan aborts in the driver's
threads).

## On the workers

| Work | Where | Why it can |
|------|-------|-----------|
| Light directions of the lightmaps (normal maps) | `ref/vk/vk_deluxe.c` | Reads the world model, writes pages allocated before; the lightmap rebuild and the map change wait for it |
| Mip levels of streamed material maps | `ref/vk/vk_stream.c` | Seeks and reads its own open file into its own staging buffer; the entry it fills is checked by generation in `done` |
| Vertices of studio models (skinning, lighting) | `ref/vk/vk_studio.c` | Reads copies of the bones and lighting and the model data, writes the dynamic ring ranges the main thread reserved; the frame runs or waits for them before it is submitted |

Files are opened and closed on the main thread: the file system's search
paths aren't thread safe, a `file_t` read by one thread at a time is.

## Measured, left on the main thread

Map loads on an SSD with warm caches (`client connected at`, amd64,
`ref_vk`): de_dust2 0.11-0.13 s (0.16-0.22 s with a 4K material pack
of every texture), c2a5 0.10 s, c1a0 0.17 s, c3a2 0.18 s. The precache
of models, sounds and textures is a small part of it, and its loaders
register into engine tables (models, sound cache, texture lists) and go
through the file system: moving them to workers would trade a few tens of
milliseconds for locks in hot code. Revisit if a profile of a cold, large
load says otherwise; the precache must still complete before the client
answers the server's signon, so the connection sequence stays the same.

Sound mixing stays where it is: `S_Update` costs little next to a frame,
and SDL already feeds the device from its own thread.

## Never on a worker

| What | Why |
|------|-----|
| Server simulation (`SV_Physics`, entity think, triggers) | Game DLLs are written for one thread and call back into the engine at any point; results must be identical to GoldSrc's |
| Client prediction (`CL_PredictMovement`, `pmove`) | Must give the server's results exactly, from the same inputs in the same order |
| `CL_Move` and user commands | Their timing and order are part of the protocol |
| Delta decoding, message parsing, net channel | Sequence numbers, acknowledgements and the order of side effects define the protocol |
| Game DLL entry points (`cdll_int`, `eiface`, studio and TriAPI callbacks) | Neither Valve's nor the SDK's libraries are thread safe |
| Renderer command recording | Done in the engine's call order, as cheap as the draws it records; only the vertices behind them moved (028) |
