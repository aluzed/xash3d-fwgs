#!/bin/sh
# Run the amd64 development build against the Steam Half-Life install.
#
# Steam data is mounted read-only through -rodir; configs, saves and our
# 64-bit game libraries live in XASH_DEV_ROOT, which takes priority over
# the read-only tree, so Steam files are never modified.
#
# Usage: run-amd64.sh [valve|cstrike] [extra engine args...]

set -e

XASH_DEV_ROOT="${XASH_DEV_ROOT:-$HOME/xash-dev/amd64}"
STEAM_HL="${STEAM_HL:-$HOME/.local/share/Steam/steamapps/common/Half-Life}"

GAME="${1:-valve}"
[ $# -gt 0 ] && shift

cd "$XASH_DEV_ROOT"
exec env XASH3D_BASEDIR="$XASH_DEV_ROOT" \
	LD_LIBRARY_PATH="$XASH_DEV_ROOT${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	./xash3d -rodir "$STEAM_HL" -game "$GAME" -dev 2 -log "$@"
