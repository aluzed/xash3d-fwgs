# 001 - Baseline build and run HL / CS 1.6 on `ref_gl`

## Context
Game data comes from the Steam install at
`~/.local/share/Steam/steamapps/common/Half-Life` (`valve/`, `cstrike/`).
The original client DLLs (`valve/cl_dlls/client.so`,
`cstrike/cl_dlls/client.so`) are **32-bit i386 only**. Server DLLs exist in
both 32-bit and 64-bit (`cs_amd64.so`). A 64-bit engine cannot load the
original client DLLs.

## Goal
Reproducible build of the engine and `ref_gl`, running HL and CS 1.6 from the
Steam data, before any Vulkan work, and a recorded decision on target
architecture.

## Scope
In: build instructions, launch scripts, architecture decision, network
compatibility check. Out: any renderer change.

## Tasks
- [x] Document required packages (amd64 host only): `libsdl2-dev`,
      `libfreetype-dev`, `libfontconfig-dev`, `libvulkan-dev`, `glslc`,
      `vulkan-tools`. **Never install `*-dev:i386` on the host** (see
      Decision).
- [ ] Build **i386** engine + `ref_gl` inside a Docker container
      (`i386/ubuntu:24.04`), loading Valve's original `client.so` /
      `hl.so` / `cs.so`. Secondary target, deferred.
- [x] Build **amd64** engine + `ref_gl` with `hlsdk-portable` (HL) and
      `cs16-client` (CS) as 64-bit game DLLs: `scripts/dev/build-amd64.sh`.
- [x] Launch script (`scripts/dev/run-amd64.sh [valve|cstrike]`): Steam
      folder mounted read-only with `-rodir`, configs and game libraries in
      the writable `XASH3D_BASEDIR` (`~/xash-dev/amd64`).
- [x] Verify (amd64): HL new game (c1a0), CS listen server on `de_dust2`, CS client
      joining a stock HLDS/ReHLDS server with `connect ip:port gs`
      (see `Documentation/goldsrc-protocol-support.md`), CS client joining a
      Xash3D dedicated server.
      Scripts: `run-rehlds.sh`, `run-xash-server.sh`.
- [x] Write `docs/building-vulkan-renderer.md` with the above.

## Acceptance criteria
- Both architectures build from a clean checkout with one documented command
  each.
- HL and CS 1.6 are playable on `ref_gl` in both architectures.
- A recorded decision (ADR in this file, "Decision" section) on which
  architecture is primary.

## Decision
**amd64 primary**, i386 secondary (2026-10-14).

- Installing `libsdl2-dev:i386` on the Ubuntu 24.04 host pulled in i386
  glib/ibus/pulse dev packages and made apt remove the desktop session and
  networking. i386 development packages are therefore banned from the host;
  any i386 build must happen in a container. The i386 *runtime* libraries
  (Vulkan loader, NVIDIA GL) installed by Steam are harmless.
- amd64 uses `hlsdk-portable` and `cs16-client` (client + ReGameDLL_CS
  server). Network compatibility with GoldSrc/Xash servers is handled by the
  engine, not by the game DLLs.
- amd64 also removes the ~4 GB address space limit that would have
  constrained 4K texture packs (story 022) and ray tracing scratch buffers
  (story 024).
- Loss: Valve's original `client.so` cannot be tested on amd64; that is the
  purpose of the deferred i386 container build.

Layout: Steam data is mounted read-only with `-rodir`; engine, game DLLs,
configs and saves live in `~/xash-dev/amd64`, which takes search priority
over the read-only tree. Steam files are never written.

## Dependencies
None.

## Risks / notes
- GoldSrc servers may flag Xash3D clients as fake clients; this is a known
  upstream limitation and not a renderer concern.
- Never redistribute Valve assets or DLLs.
- The client's emulated RevEmu2013 ticket generator failed for ~9 % of
  machine IDs and silently sent an empty ticket ("STEAM certificate length
  error! 0/1024" from any GoldSrc server). Fixed in `CL_WriteSteamTicket`
  by retrying with a rotated key prefix (2026-10-14).
