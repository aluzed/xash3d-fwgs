# 003 - Engine: `REF_VULKAN` graphics API and SDL Vulkan window

## Context
`ref_graphic_apis_t` in `engine/ref_api.h` only knows `REF_SOFTWARE` and
`REF_GL`. The engine creates the window and either a GL context or a software
surface. Vulkan needs a window created with `SDL_WINDOW_VULKAN`, the list of
required instance extensions, and a `VkSurfaceKHR` created from the window.

## Goal
The engine can create a Vulkan-capable window for a renderer that asks for
it, without affecting GL / software renderers.

## Scope
In: `ref_api.h` additions, SDL2 (and SDL3 if trivial) platform code, video
mode changes / fullscreen / resize notifications.
Out: any Vulkan object creation besides what SDL needs.

## Tasks
- [x] Add `REF_VULKAN` to `ref_graphic_apis_t` (append, do not renumber).
- [x] Add engine callbacks in `ref_api_t`:
      `Vulkan_GetInstanceExtensions( uint *count, const char **names )`,
      `Vulkan_CreateSurface( void *instance, void *surface )`,
      `Vulkan_GetDrawableSize( int *w, int *h )`, and expose
      `SDL_Vulkan_GetVkGetInstanceProcAddr` (so `ref_vk` does not link
      libvulkan directly): `Vulkan_GetVkGetInstanceProcAddr`.
- [x] Bump `REF_API_VERSION` (21) and document the change in the header changelog.
- [x] Platform SDL2: create window with `SDL_WINDOW_VULKAN`, load the Vulkan
      library (`SDL_Vulkan_LoadLibrary`), no GL context, no GL swap interval.
      SDL3 is not installed on the dev machine, so it only fails cleanly.
- [x] Null / unsupported platforms return failure cleanly so the engine falls
      back to the next renderer in the list (SDL1, SDL3, DOS, fbdev; stubs
      in `vid_common.c`).

## Acceptance criteria
- `ref_gl` and `ref_soft` behave exactly as before (regression harness clean).
- A test renderer requesting `REF_VULKAN` gets a window and a valid surface.

## Results
- `ref_gl` regression captures after the change: 30/30 bit-identical.
- `ref_soft` is not part of the default build (`RefDll('soft', ...,
  all_renderers = False)`), not re-tested.
- The Vulkan window and surface are exercised by the `ref_vk` skeleton
  (story 004): instance 1.3 created with the SDL extensions, surface
  created, RTX 2080 Ti and llvmpipe enumerated.

## Dependencies
001.

## Risks / notes
Changing `ref_api.h` is an engine/renderer contract change only; game DLLs
are not affected. Keep it upstreamable (follow `CONTRIBUTING.md`).
