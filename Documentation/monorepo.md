# One repository

Everything that makes Half-Life 1.5 and Counter-Strike 1.7 lives in this
repository since 2026-10-15: a change to the game is one commit, built and
installed from one checkout.

| Folder | What | Imported from |
|--------|------|---------------|
| `.` | the Xash3D FWGS engine, its renderers (`ref_vk`...), the dedicated server, the scripts and the installer | [FWGS/xash3d-fwgs](https://github.com/FWGS/xash3d-fwgs), fork [aluzed/xash3d-fwgs](https://github.com/aluzed/xash3d-fwgs) |
| `3rdparty/mainui` | Half-Life's menus | [aluzed/mainui_cpp](https://github.com/aluzed/mainui_cpp) `master` @ 16a6c4a (fork of FWGS/mainui_cpp) |
| `3rdparty/mainui/miniutl` | | FWGS/miniutl @ 048a416 |
| `halflife-15` | Half-Life's game libraries (hlsdk-portable) | [FWGS/hlsdk-portable](https://github.com/FWGS/hlsdk-portable) `master` @ 6c168fc |
| `halflife-15/freevgui` | | FWGS/freevgui @ 73bfb46 |
| `cstrike-17` | Counter-Strike: client, extras, build | [aluzed/cs16-client](https://github.com/aluzed/cs16-client) `main` @ a08796b (fork of Velaron/cs16-client) |
| `cstrike-17/3rdparty/ReGameDLL_CS` | Counter-Strike's server game library: weapons, purchases, the molotov | Velaron/ReGameDLL_CS @ 7be9d59, with the commits of story 054 (molotov, full ammo) on top: 387ef1d, bc72398, 9b0739e |
| `cstrike-17/3rdparty/ReGameDLL_CS/regamedll/extra/cssdk` | | s1lentq/CSSDK @ a09ee09 |
| `cstrike-17/3rdparty/mainui_cpp` | Counter-Strike's menus (buy menu, crosshair options) | [aluzed/mainui_cpp](https://github.com/aluzed/mainui_cpp) `crosshair-options` @ 0d6cb52 |
| `cstrike-17/3rdparty/mainui_cpp/miniutl` | | FWGS/miniutl @ 048a416 |
| `cstrike-17/3rdparty/miniutl` | | FWGS/MiniUTL @ 371731e |
| `cstrike-17/3rdparty/yapb` | the bots | yapb/yapb @ 4967a22 |
| `cstrike-17/3rdparty/yapb/ext/crlib`, `ext/linkage` | | yapb/crlib @ 7efccdd, yapb/linkage @ a21540a |

The other folders of `3rdparty/` are libraries used as they are (codecs,
Vulkan headers, SMAA...): they stay git submodules, `git submodule update
--init --recursive` fetches them.

The imported folders are plain files, without their history: what was
imported is the commit above. The forks they came from are no longer where
the work happens.

## Building

- Engine: `docs/building-vulkan-renderer.md` (waf).
- Games, each into a build directory out of the tree:

  ```
  cmake -S halflife-15 -B <build>/halflife-15 -D64BIT=ON
  cmake -S cstrike-17  -B <build>/cstrike-17
  cmake --build <build>/... --config Release
  cmake --install <build>/... --config Release --prefix <engine install directory>
  ```

  On Windows with Visual Studio 2026 add `-G "Visual Studio 18 2026" -A x64`,
  and `-DENABLE_YY_THUNKS=OFF` for `cstrike-17`.
- Linux development tree: `scripts/dev/build-amd64.sh` builds all of it.
- Windows installer: `scripts/installer/windows`.

## Taking an upstream change

Fetch the upstream repository at a newer commit, compare it with the commit
imported (the table above), and apply the difference to the folder:

```
git clone https://github.com/yapb/yapb /tmp/yapb
git -C /tmp/yapb diff 4967a22 <new commit> > /tmp/yapb.patch
git apply --directory=cstrike-17/3rdparty/yapb /tmp/yapb.patch
```

then update the commit in the table.
