#!/bin/sh
# Build the amd64 development tree: engine + ref_gl + ref_vk, Xash dedicated server, hlsdk-portable (HL)
# and cs16-client (CS 1.6, server via ReGameDLL_CS, YaPB bots).
#
# Output goes to XASH_DEV_ROOT (default ~/xash-dev/amd64), game sources are
# cloned into XASH_DEV_SRC (default ~/xash-dev/src). Run from anywhere.

set -e

ENGINE_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
XASH_DEV_ROOT="${XASH_DEV_ROOT:-$HOME/xash-dev/amd64}"
XASH_DEV_SRC="${XASH_DEV_SRC:-$HOME/xash-dev/src}"
XASH_DEV_DEDICATED="${XASH_DEV_DEDICATED:-$HOME/xash-dev/dedicated}"
JOBS="${JOBS:-$(nproc)}"

clone() {
	[ -d "$XASH_DEV_SRC/$1" ] || git clone --recursive "$2" "$XASH_DEV_SRC/$1"
}

mkdir -p "$XASH_DEV_ROOT" "$XASH_DEV_SRC" "$XASH_DEV_DEDICATED"

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
clone hlsdk-portable https://github.com/FWGS/hlsdk-portable.git
cd "$XASH_DEV_SRC/hlsdk-portable"
./waf configure -T release -8
./waf build -j"$JOBS"
mkdir -p "$XASH_DEV_ROOT/valve/cl_dlls" "$XASH_DEV_ROOT/valve/dlls"
cp build/cl_dll/client_amd64.so "$XASH_DEV_ROOT/valve/cl_dlls/"
cp build/dlls/hl_amd64.so "$XASH_DEV_ROOT/valve/dlls/"

# Counter-Strike 1.6 game libraries.
clone cs16-client https://github.com/Velaron/cs16-client.git
cd "$XASH_DEV_SRC/cs16-client"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$JOBS"
cmake --install build --prefix "$XASH_DEV_ROOT"

echo "amd64 tree ready in $XASH_DEV_ROOT"
