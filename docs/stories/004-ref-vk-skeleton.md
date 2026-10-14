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
- [ ] `RefDll('vk', False)` in `wscript`; `ref/vk/wscript` checking for
      Vulkan headers (vendored `3rdparty/Vulkan-Headers` submodule preferred
      for reproducibility).
- [ ] Layout: `vk_main.c` (GetRefAPI, init/shutdown), `vk_core.*`,
      `vk_swapchain.*`, `vk_memory.*`, `vk_textures.*`, `vk_2d.*`,
      `vk_world.*`, `vk_studio.*`, `vk_sprite.*`, `vk_triapi.*`,
      `vk_beams.*`, `vk_decals.*`, `shaders/`.
- [ ] Reuse `ref/common` (lighting, math, skybox, image helpers) where it
      helps; the internal design does not need to mirror `ref_gl`, only the
      observable behaviour does.
- [ ] Stub every interface function, logging unimplemented calls once
      (`r_vk_log_stubs` cvar).
- [ ] Studio model interface (`R_StudioFillAPI`) wired so client DLLs that
      replace the studio renderer (CS does) do not crash.

## Acceptance criteria
- `-ref vk` loads the module, opens a black window, reaches the main menu
  logic (invisible) and quits cleanly; no crash when starting a map.
- Builds on i386 and amd64.

## Dependencies
003.
