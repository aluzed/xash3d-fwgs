# Building and running the ref_vk development tree

This is the setup used to develop the Vulkan renderer (`ref/vk`) for
Half-Life and Counter-Strike 1.6. The roadmap lives in `docs/stories/`.

## Target architecture

**amd64 is the primary target.** See the decision in
`docs/stories/001-baseline-build-and-run.md`. In short: Valve's original
client libraries are 32-bit only, but installing i386 development packages
on an Ubuntu 24.04 host broke the desktop and networking, and a 32-bit
process would cap 4K textures and ray tracing memory at ~4 GB. The game
libraries come from `hlsdk-portable` (HL) and `cs16-client` (CS 1.6) built
as 64-bit. An i386 build, if needed, must be done inside a container.

## Host packages (amd64 only)

```
sudo apt-get install --no-remove libsdl2-dev libfreetype-dev \
	libfontconfig-dev libvulkan-dev glslc vulkan-tools gcc-multilib cmake
```

Rules:

- Never install `*-dev:i386` packages on the host.
- Simulate first with `apt-get -s install ...` and make sure the output has
  no `Remv` line. `--no-remove` makes apt abort instead of removing
  anything.
- The i386 runtime libraries installed by Steam (Vulkan loader, NVIDIA GL)
  are harmless and must be left alone.

## Game data

Game data comes from the Steam install, mounted read-only:

```
~/.local/share/Steam/steamapps/common/Half-Life/{valve,cstrike}
```

Steam files are never written. Everything the engine writes (configs,
saves, logs) and our 64-bit game libraries live in `~/xash-dev/amd64`, which
takes search priority over the read-only tree passed with `-rodir`.

## Build

One command builds everything:

```
scripts/dev/build-amd64.sh
```

It produces:

| Directory              | Content                                              |
|------------------------|------------------------------------------------------|
| `~/xash-dev/amd64`     | engine, `ref_gl`, menu, `valve/` and `cstrike/` libs |
| `~/xash-dev/dedicated` | Xash dedicated server                                |
| `~/xash-dev/src`       | `hlsdk-portable` and `cs16-client` checkouts         |

The dedicated server is configured in `build-dedicated/`; the script builds
it before the client so that a plain `./waf build` still targets the
client in `build/`. To iterate on the engine only:

```
./waf build -j$(nproc) && ./waf install --destdir="$HOME/xash-dev/amd64"
```

## Run

```
scripts/dev/run-amd64.sh valve                # Half-Life
scripts/dev/run-amd64.sh cstrike +map de_dust2  # CS 1.6 listen server
```

Extra arguments are passed to the engine. The engine catches SIGTERM and
SIGINT to show the quit dialog; quit from the menu or the console.

## Network compatibility checks

| Server                  | Start                             | Join from the client console  |
|-------------------------|-----------------------------------|-------------------------------|
| Xash dedicated server   | `scripts/dev/run-xash-server.sh`  | `connect 127.0.0.1:27016`     |
| ReHLDS (GoldSrc 48)     | `scripts/dev/run-rehlds.sh`       | `connect 127.0.0.1:27015 gs`  |

`run-rehlds.sh` uses the HLDS install in `/var/www/ReHLDS/.bench/hlds`
(override with `HLDS_DIR`). It starts with `sv_lan 1`, which lets local
clients in without Steam validating their emulated ticket. Joining an
internet GoldSrc server requires the Steam broker described in
`Documentation/goldsrc-protocol-support.md`.

## Known harmless log messages

- `Failed to load vgui_support library ... vgui.so: wrong ELF class` —
  Steam only ships a 32-bit `vgui.so`.
- `Failed to load 'maps/de_dust2.nav'` — YaPB has no navigation file yet
  for that map.
- `CL_LoadProgs: failed to get address of ... proc` — optional client
  exports.
