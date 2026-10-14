#!/bin/sh
# Build the i386 engine with ref_gl and ref_vk inside a Docker container,
# into XASH_DEV_I386 (default ~/xash-dev/i386). Half-Life runs with Valve's
# original 32-bit game libraries from the Steam install (-rodir), see
# run-i386.sh. Counter-Strike gets cs16-client built for i386 as well when
# its sources are in XASH_DEV_SRC (build-amd64.sh clones them): the current
# Steam client.so of CS doesn't start on Xash (it crashes in HUD_Init).
# Nothing is installed on the host.

set -e

ENGINE_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
XASH_DEV_I386="${XASH_DEV_I386:-$HOME/xash-dev/i386}"
XASH_DEV_SRC="${XASH_DEV_SRC:-$HOME/xash-dev/src}"
JOBS="${JOBS:-$(nproc)}"
IMAGE=xash3d-fwgs-i386-build

mkdir -p "$XASH_DEV_I386"
docker build -t "$IMAGE" -f "$ENGINE_SRC/scripts/dev/docker/Dockerfile.i386" "$ENGINE_SRC/scripts/dev/docker"

# the sources are copied: waf keeps its lock file in the source root,
# where the host's own configuration is
docker run --rm --user "$(id -u):$(id -g)" \
	-v "$ENGINE_SRC:/src:ro" -v "$XASH_DEV_I386:/out" \
	-e HOME=/tmp -e JOBS="$JOBS" "$IMAGE" sh -c '
		set -e
		rsync -a --exclude="/build*" --exclude="/.lock-waf*" --exclude="/.waf*" /src/ /tmp/src/
		cd /tmp/src
		./waf configure -T release --enable-vk
		./waf build -j"$JOBS"
		./waf install --destdir=/out
	'

if [ -d "$XASH_DEV_SRC/cs16-client" ]; then
	docker run --rm --user "$(id -u):$(id -g)" \
		-v "$XASH_DEV_SRC/cs16-client:/src:ro" -v "$XASH_DEV_I386:/out" \
		-e HOME=/tmp -e JOBS="$JOBS" "$IMAGE" sh -c '
			set -e
			rsync -a --exclude="/build*" /src/ /tmp/src/
			cd /tmp/src
			cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
			cmake --build build -j"$JOBS"
			cmake --install build --prefix /out
		'
fi

echo "i386 engine in $XASH_DEV_I386"
