# 004 - `ref_vk` module skeleton

## Context
Renderers are shared libraries exporting `GetRefAPI`, registered in the
top-level `wscript` (`REFDLLS`). `ref/null` is the smallest example.

## Goal
A `ref/vk` module that builds, is loaded by the engine with `-ref vk`, and
implements every `ref_interface_t` entry as a safe stub.

## Scope
In: build integration, directory layout, logging, cvar registration, stubs.
Out: actual rendering.

## Tasks
- [x] `RefDll('vk', False)` in `wscript`; `ref/vk/wscript` checking for
      Vulkan headers (vendored `3rdparty/Vulkan-Headers` submodule, pinned
      to v1.3.275).
- [~] Layout: `vk_main.c` (GetRefAPI, init/shutdown), `vk_core.*`,
      `vk_swapchain.*`, `vk_memory.*`, `vk_textures.*`, `vk_2d.*`,
      `vk_world.*`, `vk_studio.*`, `vk_sprite.*`, `vk_triapi.*`,
      `vk_beams.*`, `vk_decals.*`, `shaders/`. Created so far:
      `vk_local.h`, `vk_main.c`, `vk_core.c`, `vk_textures.c`; the others
      come with their stories instead of as empty files.
- [x] Reuse `ref/common` (lighting, math, skybox, image helpers) where it
      helps; the internal design does not need to mirror `ref_gl`, only the
      observable behaviour does.
- [x] Stub every interface function, logging unimplemented calls once
      (`r_vk_log_stubs` cvar). RenderAPI and TriAPI entries are filled with
      safe stubs too: cs16-client calls them without NULL checks. Texture
      loads return real handles from a name table (cs16-client aborts the
      map on a 0 handle).
- [x] Studio model interface (`R_StudioFillAPI`) wired so client DLLs that
      replace the studio renderer (CS does) do not crash: it returns false
      for now, so the engine skips the client DLL negotiation (story 017).

## Acceptance criteria
- `-ref vk` loads the module, opens a black window, reaches the main menu
  logic (invisible) and quits cleanly; no crash when starting a map.
- Builds on i386 and amd64.

## Results
- `-ref vk`: HL c1a0 and CS de_dust2 load, the client connects, `quit`
  exits cleanly (exit code 0). Nothing is drawn yet (no swapchain).
- Only built on amd64; the i386 build is deferred with the container
  (story 001).

## Dependencies
003.
