#!/bin/sh
# Run the amd64 Xash dedicated server on port 27016, sharing the game
# libraries of the development tree and the read-only Steam data.
# Join it from the client with: connect 127.0.0.1:27016
#
# Usage: run-xash-server.sh [game] [map] [extra server args...]

set -e

XASH_DEV_ROOT="${XASH_DEV_ROOT:-$HOME/xash-dev/amd64}"
XASH_DEV_DEDICATED="${XASH_DEV_DEDICATED:-$HOME/xash-dev/dedicated}"
STEAM_HL="${STEAM_HL:-$HOME/.local/share/Steam/steamapps/common/Half-Life}"

GAME="${1:-cstrike}"
[ $# -gt 0 ] && shift
MAP="${1:-de_dust2}"
[ $# -gt 0 ] && shift

cd "$XASH_DEV_ROOT"
exec env XASH3D_BASEDIR="$XASH_DEV_ROOT" \
	"$XASH_DEV_DEDICATED/xash" -rodir "$STEAM_HL" -game "$GAME" \
	-port 27016 +maxplayers 12 +map "$MAP" "$@"
