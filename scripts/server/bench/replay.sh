#!/bin/bash
# Story 037: replays a session recorded by bench.sh (RECORD=1) with the
# installed engine, as fast as possible, and compares what the server sent with
# the recording, per destination.
#
# usage: scripts/server/bench/replay.sh <bench results directory> [server args...]
#
# Extra server args are added to the recorded command line (e.g. an option to
# check that it doesn't change the bytes sent). Exit code 0 when identical.

set -euo pipefail

DIR=$1
shift
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR=${XASH_SERVER_ROOT:-$HOME/xash-dev/server}/cstrike

[ -f "$DIR/session" ] || { echo "replay.sh: $DIR was not recorded (RECORD=1)" >&2; exit 1; }

env_line=$(head -1 "$DIR/session")
mapfile -t args < <(tail -n +2 "$DIR/session" | grep -v -e '^-netrecord$' -e '^-netsends$' -e "^$DIR/")
cp "$DIR/bench.cfg" "$GAME_DIR/bench.cfg"

# shellcheck disable=SC2086
env NO_BUILD=1 $env_line "$HERE/../run.sh" "${args[@]}" "$@" \
	-netreplay "$DIR/inputs.bin" -netsends "$DIR/sends-replay.bin" < /dev/null > "$DIR/replay.log" 2>&1

python3 "$HERE/replay_compare.py" "$DIR/sends.bin" "$DIR/sends-replay.bin"
