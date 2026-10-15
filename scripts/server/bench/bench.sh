#!/bin/bash
# Story 036: one benchmark run of the dedicated server, with load clients,
# bots or a flood, and the frame profiler (035). The server tree of
# scripts/server/setup.sh runs on its own port with the engine installed there.
#
# usage: scripts/server/bench/bench.sh <label> [server args...]
#
# Environment:
#   CLIENTS=32       load clients (loadclient.py --play), 0 for none
#   BOTS=0           bots (bot_quota)
#   UPDATERATE=100   cl_updaterate and cmdrate of the load clients
#   SPLIT=1          load clients ask for split datagrams, like real clients
#   FLOOD=           a2s, challenge or junk: 50 000 packets/s from 4096 addresses
#   FEATURES=1       options of the dedicated server stories on (run.sh)
#   WARMUP=20  DURATION=30  PORT=27018  MAP=de_dust2
#   OUT              results directory (default ~/xash-dev/bench/<date>-<label>)
#   NO_BUILD=1       run the installed engine, else build and install it first
#   RECORD=1         record the session (story 037): inputs.bin, sends.bin,
#                    replayed by replay.sh
#
# Results: profile.csv (every frame), report.txt (sv_profile), clients.json,
# flood.json, cpu.txt (CPU of the process over the capture), server.log.

set -euo pipefail

label=$1
shift
HERE="$(cd "$(dirname "$0")" && pwd)"
CLIENTS=${CLIENTS:-32}
BOTS=${BOTS:-0}
UPDATERATE=${UPDATERATE:-100}
WARMUP=${WARMUP:-20}
DURATION=${DURATION:-30}
PORT=${PORT:-27018}
OUT=${OUT:-$HOME/xash-dev/bench/$(date +%Y%m%d-%H%M%S)-$label}
GAME_DIR=${XASH_SERVER_ROOT:-$HOME/xash-dev/server}/cstrike

mkdir -p "$OUT"
[ "${NO_BUILD:-0}" = 1 ] || INSTALL_ONLY=1 "$HERE/../run.sh" > "$OUT/build.log" 2>&1
fifo=$OUT/console
mkfifo "$fifo"
trap 'rm -f "$fifo"' EXIT

# server.cfg runs after the command line: the run's settings come after it
printf 'exec server.cfg\nbot_quota %s\nsv_maxupdaterate %s\nmp_timelimit 0\n' "$BOTS" "$UPDATERATE" > "$GAME_DIR/bench.cfg"

maxplayers=$(( CLIENTS + BOTS > 2 ? CLIENTS + BOTS : 2 ))
if [ "${RECORD:-0}" = 1 ]; then
	set -- "$@" -netrecord "$OUT/inputs.bin" -netsends "$OUT/sends.bin"
	# what replay.sh needs to start the same server
	{
		echo "PORT=$PORT MAXPLAYERS=$maxplayers MAP=${MAP:-de_dust2} FEATURES=${FEATURES:-1}"
		printf '%s\n' +servercfgfile bench.cfg "$@"
	} > "$OUT/session"
	cp "$GAME_DIR/bench.cfg" "$OUT/bench.cfg"
fi
NO_BUILD=1 PORT=$PORT MAXPLAYERS=$maxplayers MAP=${MAP:-de_dust2} \
	"$HERE/../run.sh" +servercfgfile bench.cfg "$@" < "$fifo" > "$OUT/server.log" 2>&1 &
pid=$! # run.sh execs the server
exec 3> "$fifo"

console() {
	echo "$*" >&3
}

sleep 10

if [ "$CLIENTS" -gt 0 ]; then
	python3 "$HERE/loadclient.py" --server "127.0.0.1:$PORT" --clients "$CLIENTS" --play ${SPLIT:+--split} \
		--cmdrate "$UPDATERATE" --updaterate "$UPDATERATE" \
		--measure-at $(( $(date +%s) + WARMUP + 2 )) --duration "$DURATION" \
		--output "$OUT/clients.json" > /dev/null 2> "$OUT/clients.log" &
fi

sleep "$WARMUP"

if [ -n "${FLOOD:-}" ]; then
	python3 "$HERE/flood.py" --server "127.0.0.1:$PORT" --kind "$FLOOD" --pps 50000 --sources 4096 \
		--duration $(( DURATION + 4 )) --output "$OUT/flood.json" > /dev/null 2>&1 &
fi

sleep 2
t0=$(awk '{print $14+$15}' /proc/$pid/stat)
console "sv_profile_reset"
console "sv_profile $DURATION"
sleep $(( DURATION + 1 ))
t1=$(awk '{print $14+$15}' /proc/$pid/stat)
console "sv_profile_csv bench-profile.csv"
sleep 1
echo "$(( t1 - t0 )) $(getconf CLK_TCK) $DURATION" | awk '{printf "cpu %.1f %% of a core\n", 100 * $1 / $2 / $3}' > "$OUT/cpu.txt"

wait $(jobs -p | grep -v "^$pid$") 2> /dev/null || true
console "quit"
exec 3>&-
wait

mv "$GAME_DIR/bench-profile.csv" "$OUT/profile.csv"
sed 's/\x1b\[[0-9;]*m//g; s/^\[[0-9:]*\] //' "$OUT/server.log" | sed -n '/^sv_profile: [0-9]* frames/,/^early_read_us/p' > "$OUT/report.txt"

echo "== $label ($OUT)"
cat "$OUT/cpu.txt"
grep -E "sv_profile:|tick_interval|^frame|read_packets|physics|send_messages|datagram|gather|encode|transmit" "$OUT/report.txt" || true
[ -f "$OUT/clients.json" ] && python3 -c "
import json; d = json.load(open('$OUT/clients.json'))
print('clients {ingame}/{clients} in game, {snapshot_rate_per_client:.1f} snapshots/s each, interval p99 {interval_ms_p99:.2f} ms, input latency avg {input_latency_ms_avg:.2f} ms'.format(**d))
" || true
