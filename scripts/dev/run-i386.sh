#!/bin/sh
# Run the i386 build (build-i386.sh) on the host with Valve's original
# 32-bit game libraries from the Steam install, mounted read-only through
# -rodir. Needs the i386 runtime libraries Steam installs (SDL2, Vulkan
# loader, GL driver), never their development packages; the original
# client.so needs libsteam_api.so, next to hl_linux in the Steam install.
#
# Counter-Strike runs cs16-client when build-i386.sh installed it in the
# development tree. CLIENT=steam runs Steam's own client.so instead, with
# its VGUI2 interface (vgui2_support): it needs the Steam client running,
# its SteamAPI_Init fails otherwise and it quits, as it does on GoldSrc.
#
# Usage: [CLIENT=steam] run-i386.sh [valve|cstrike] [extra engine args...]

set -e

XASH_DEV_I386="${XASH_DEV_I386:-$HOME/xash-dev/i386}"
STEAM_HL="${STEAM_HL:-$HOME/.local/share/Steam/steamapps/common/Half-Life}"

GAME="${1:-valve}"
[ $# -gt 0 ] && shift

if [ "$CLIENT" = steam ]; then
	# the Steam application the client says it is, as Steam's launcher sets it
	case "$GAME" in
		cstrike) APPID=10 ;;
		*) APPID=70 ;;
	esac
	set -- -clientlib "$STEAM_HL/$GAME/cl_dlls/client.so" "$@"
	export SteamAppId="${SteamAppId:-$APPID}" SteamGameId="${SteamGameId:-$APPID}"
fi

cd "$XASH_DEV_I386"
exec env XASH3D_BASEDIR="$XASH_DEV_I386" \
	LD_LIBRARY_PATH="$XASH_DEV_I386:$STEAM_HL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	./xash3d -rodir "$STEAM_HL" -game "$GAME" -dev 2 -log "$@"
