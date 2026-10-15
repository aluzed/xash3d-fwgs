#!/bin/sh
# Builds this checkout's dedicated server (amd64), installs it into the server
# tree of setup.sh and starts Counter-Strike with Metamod-FWGS, 19 bots and the
# deathmatch mode (story 034). The options of the dedicated server stories are
# added here as they land, every one on (story 049).
#
# usage: scripts/server/run.sh [extra server args...]
#
# Environment:
#   XASH_SERVER_ROOT  server tree (default ~/xash-dev/server)
#   STEAM_HL          Steam Half-Life install, read-only
#   MAP=de_dust2  PORT=27016  MAXPLAYERS=20  TICRATE=1000
#   RCON_PASSWORD     rcon password (default: none, rcon off)
#   NO_BUILD=1        start the installed engine as it is
#   SPIN_US=200       busy-wait before each tick (burns CPU, only for cores
#                     dedicated to the server; 0 to sleep)
#   FEATURES=0        every option of the stories off, for comparisons
#   METAMOD=0         load the game library without Metamod

set -e

ENGINE_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
XASH_SERVER_ROOT="${XASH_SERVER_ROOT:-$HOME/xash-dev/server}"
STEAM_HL="${STEAM_HL:-$HOME/.local/share/Steam/steamapps/common/Half-Life}"

[ -f "$XASH_SERVER_ROOT/cstrike/dlls/cs_amd64.so" ] || { echo "run.sh: run scripts/server/setup.sh first" >&2; exit 1; }

if [ "${NO_BUILD:-0}" != 1 ]; then
	# Own waf lock file: a plain ./waf build keeps building the client tree
	cd "$ENGINE_SRC"
	export WAFLOCK=.lock-waf_dedicated
	[ -f "$WAFLOCK" ] || ./waf configure -o build-dedicated -T release -8 -d
	./waf build -j"$(nproc)"
	./waf install --destdir="$XASH_SERVER_ROOT"
fi

set -- -game cstrike -port "${PORT:-27016}" +maxplayers "${MAXPLAYERS:-20}" \
	-sys_ticrate "${TICRATE:-1000}" "$@"

# Options of the dedicated server stories, every one on
if [ "${FEATURES:-1}" = 1 ]; then
	set -- "$@" \
		+sys_scheduler 1 +sys_spin_us "${SPIN_US:-200}" \
		+sv_snapshot_spread 1 +sv_log_async 1
else
	set -- "$@" +sys_scheduler 0 +sv_snapshot_spread 0 +sv_log_async 0
fi
[ "${METAMOD:-1}" = 1 ] && set -- -dll addons/metamod/metamod_amd64.so "$@"
[ -n "${RCON_PASSWORD:-}" ] && set -- "$@" +rcon_password "$RCON_PASSWORD"

cd "$XASH_SERVER_ROOT"
exec env XASH3D_BASEDIR="$XASH_SERVER_ROOT" ./xash -rodir "$STEAM_HL" "$@" +map "${MAP:-de_dust2}"
