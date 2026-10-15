#!/bin/sh
# Build the amd64 development tree: engine + ref_gl + ref_vk, Xash dedicated server, halflife-15 (HL,
# hlsdk-portable) and cstrike-17 (CS, cs16-client with its server ReGameDLL_CS and the YaPB bots), all
# from this repository.
#
# Output goes to XASH_DEV_ROOT (default ~/xash-dev/amd64), the games' build
# directories to XASH_DEV_BUILD (default ~/xash-dev/build). Run from anywhere.

set -e

ENGINE_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
XASH_DEV_ROOT="${XASH_DEV_ROOT:-$HOME/xash-dev/amd64}"
XASH_DEV_BUILD="${XASH_DEV_BUILD:-$HOME/xash-dev/build}"
XASH_DEV_DEDICATED="${XASH_DEV_DEDICATED:-$HOME/xash-dev/dedicated}"
JOBS="${JOBS:-$(nproc)}"

mkdir -p "$XASH_DEV_ROOT" "$XASH_DEV_BUILD" "$XASH_DEV_DEDICATED"

# Xash dedicated server, in its own build directory. Built before the client
# because waf keeps the last configured directory as the default one, and
# a plain `./waf build` must keep building the client.
cd "$ENGINE_SRC"
./waf configure -o build-dedicated -T release -8 -d
./waf build -j"$JOBS"
./waf install --destdir="$XASH_DEV_DEDICATED"

# Engine. --destdir alone keeps the tree flat (no prefix nesting).
./waf configure -T release -8 --enable-vk
./waf build -j"$JOBS"
./waf install --destdir="$XASH_DEV_ROOT"

# Half-Life game libraries.
cmake -S "$ENGINE_SRC/halflife-15" -B "$XASH_DEV_BUILD/halflife-15" -DCMAKE_BUILD_TYPE=Release -D64BIT=ON
cmake --build "$XASH_DEV_BUILD/halflife-15" -j"$JOBS"
cmake --install "$XASH_DEV_BUILD/halflife-15" --prefix "$XASH_DEV_ROOT"

# Counter-Strike game libraries: client, menu, server (ReGameDLL_CS), bots.
cmake -S "$ENGINE_SRC/cstrike-17" -B "$XASH_DEV_BUILD/cstrike-17" -DCMAKE_BUILD_TYPE=Release
cmake --build "$XASH_DEV_BUILD/cstrike-17" -j"$JOBS"
cmake --install "$XASH_DEV_BUILD/cstrike-17" --prefix "$XASH_DEV_ROOT"

echo "amd64 tree ready in $XASH_DEV_ROOT"
