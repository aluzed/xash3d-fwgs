#!/bin/sh
# Run a local ReHLDS (GoldSrc protocol 48) server to test the client against
# a stock-compatible dedicated server. Join it from the client with:
#   connect 127.0.0.1:27015 gs
#
# sv_lan 1 lets local clients in without Steam validating their ticket.
#
# Usage: run-rehlds.sh [map] [extra server args...]

set -e

HLDS_DIR="${HLDS_DIR:-/var/www/ReHLDS/.bench/hlds}"
MAP="${1:-de_dust2}"
[ $# -gt 0 ] && shift

cd "$HLDS_DIR"
exec env LD_LIBRARY_PATH=".${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	./hlds_linux -game cstrike -port 27015 -insecure \
	+sv_lan 1 +maxplayers 12 +map "$MAP" "$@"
