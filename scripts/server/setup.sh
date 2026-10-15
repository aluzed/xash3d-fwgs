#!/bin/sh
# Story 034: the Counter-Strike dedicated server tree, next to the read-only
# Steam data. Engine from this checkout (amd64, dedicated), game library from
# the amd64 development tree (scripts/dev/build-amd64.sh: cs16-client's
# ReGameDLL_CS), Metamod-FWGS, configs from scripts/server/cstrike and the bot
# navigation files.
#
# usage: scripts/server/setup.sh
#
# Environment:
#   XASH_SERVER_ROOT  server tree (default ~/xash-dev/server)
#   XASH_DEV_ROOT     amd64 development tree with cstrike/dlls/cs_amd64.so
#                     (default ~/xash-dev/amd64)
#   STEAM_HL          Steam Half-Life install (default ~/.local/share/Steam/steamapps/common/Half-Life)
#   NAV_DIR           .nav files to copy (default: none)

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
XASH_SERVER_ROOT="${XASH_SERVER_ROOT:-$HOME/xash-dev/server}"
XASH_DEV_ROOT="${XASH_DEV_ROOT:-$HOME/xash-dev/amd64}"
STEAM_HL="${STEAM_HL:-$HOME/.local/share/Steam/steamapps/common/Half-Life}"
METAMOD_URL="https://github.com/FWGS/metamod-fwgs/releases/download/continuous/metamod-fwgs_linux_x64.zip"

[ -d "$STEAM_HL/cstrike" ] || { echo "setup.sh: no Counter-Strike in $STEAM_HL" >&2; exit 1; }
[ -f "$XASH_DEV_ROOT/cstrike/dlls/cs_amd64.so" ] || { echo "setup.sh: no cs_amd64.so in $XASH_DEV_ROOT, run scripts/dev/build-amd64.sh" >&2; exit 1; }

game="$XASH_SERVER_ROOT/cstrike"
mkdir -p "$game/dlls" "$game/maps" "$game/addons/metamod"
cp "$XASH_DEV_ROOT/cstrike/dlls/cs_amd64.so" "$game/dlls/"
# cs16-client extras: bot profiles and chatter (not in the Steam data)
cp "$XASH_DEV_ROOT/cstrike/extras.pk3" "$game/"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/metamod.zip" "$METAMOD_URL"
unzip -q -o "$tmp/metamod.zip" -d "$tmp/metamod"
cp "$tmp/metamod/addons/metamod/metamod_amd64.so" "$game/addons/metamod/"

cp "$HERE/cstrike/metamod-config.ini" "$game/addons/metamod/config.ini"
[ -f "$game/addons/metamod/plugins.ini" ] || cp "$HERE/cstrike/metamod-plugins.ini" "$game/addons/metamod/plugins.ini"
for f in game_init.cfg server.cfg csdm.cfg mapcycle.txt; do
	cp "$HERE/cstrike/$f" "$game/$f"
done

if [ -n "${NAV_DIR:-}" ]; then
	cp "$NAV_DIR"/*.nav "$game/maps/"
fi

echo "server tree ready in $XASH_SERVER_ROOT"
