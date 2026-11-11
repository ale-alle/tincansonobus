#!/bin/sh
# Builds canduet-server on Linux (g++ 9+). Used by the Docker image (server/docker/Dockerfile);
# server/vps/install-relay.sh compiles the same files.
#   sh server/relay/build.sh <repo root> <output file>
set -eu
SRC=${1:?usage: build.sh <repo root> <output file>}
OUT=${2:?usage: build.sh <repo root> <output file>}
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

A="$SRC/deps/aoo/lib/src"
OSC="$SRC/deps/aoo/deps/oscpack/osc"
FLAGS="-std=c++17 -O2 -pthread -DAOO_STATIC -I$SRC/deps/aoo/lib -I$A -I$SRC/deps/aoo/deps -I$SRC/server/relay"
for f in "$SRC/server/relay/main.cpp" "$SRC/server/relay/relay.cpp" "$SRC/server/relay/group_auth.cpp" "$A/server.cpp" "$A/client.cpp" \
         "$A/net_utils.cpp" "$A/common.cpp" "$A/codec_pcm.cpp" "$A/time.cpp" "$A/sync.cpp" \
         "$OSC/OscOutboundPacketStream.cpp" "$OSC/OscReceivedElements.cpp" "$OSC/OscTypes.cpp"; do
    # shellcheck disable=SC2086
    g++ $FLAGS -c "$f" -o "$BUILD/$(basename "$f").o"
done
gcc -O2 -c "$SRC/deps/aoo/deps/md5/md5.c" -o "$BUILD/md5.o"
mkdir -p "$(dirname "$OUT")"
g++ -pthread "$BUILD"/*.o -o "$OUT"
echo "built $OUT"
