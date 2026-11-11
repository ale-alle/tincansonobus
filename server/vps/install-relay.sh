#!/bin/bash
# Replaces the stock connection server with canduet-server (connection server + lesson audio relay,
# doc/TINCAN-RELAY.md). Same port, so apps need no new settings. Safe to run again.
# Run as root on the server, from the folder made by scripts/make-relay-upload.ps1:
#   bash install-relay.sh <server public IPv4>
# Undo: systemctl disable --now canduet-server; systemctl enable --now aooserver
set -euo pipefail

RELAY_IP=${1:?usage: install-relay.sh <server public IPv4>}
PORT=10998
RELAY_PORTS=40000:40999
SRC=$(cd "$(dirname "$0")" && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

# Compile first: nothing on the server changes if this fails
A="$SRC/deps/aoo/lib/src"
OSC="$SRC/deps/aoo/deps/oscpack/osc"
FLAGS="-std=c++17 -O2 -pthread -DAOO_STATIC -I$SRC/deps/aoo/lib -I$A -I$SRC/deps/aoo/deps -I$SRC/server/relay"
for f in "$SRC/server/relay/main.cpp" "$SRC/server/relay/relay.cpp" "$SRC/server/relay/group_auth.cpp" "$A/server.cpp" "$A/client.cpp" \
         "$A/net_utils.cpp" "$A/common.cpp" "$A/codec_pcm.cpp" "$A/time.cpp" "$A/sync.cpp" \
         "$OSC/OscOutboundPacketStream.cpp" "$OSC/OscReceivedElements.cpp" "$OSC/OscTypes.cpp"; do
    g++ $FLAGS -c "$f" -o "$BUILD/$(basename "$f").o" &
done
wait
gcc -O2 -c "$SRC/deps/aoo/deps/md5/md5.c" -o "$BUILD/md5.o"
g++ -pthread "$BUILD"/*.o -o "$BUILD/canduet-server"

install -m 755 "$BUILD/canduet-server" /usr/local/bin/canduet-server
/usr/local/bin/canduet-server --self-test

# T-048: only lesson groups signed with this secret are created; the rooms-worker gets the same value
# (wrangler secret put RELAY_GROUP_SECRET). Kept across reinstalls.
install -d -m 755 /etc/canduet
[ -s /etc/canduet/group-secret ] || (umask 077; openssl rand -hex 32 > /etc/canduet/group-secret)

cat > /etc/systemd/system/canduet-server.service <<EOF
[Unit]
Description=CanDuet connection server and audio relay
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/canduet-server --relay-ip $RELAY_IP --port $PORT --relay-ports ${RELAY_PORTS/:/-} --group-secret-file %d/group-secret
LoadCredential=group-secret:/etc/canduet/group-secret
LimitNOFILE=8192
DynamicUser=yes
Restart=always
RestartSec=2
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
EOF

ufw allow "$RELAY_PORTS/udp"

systemctl daemon-reload
systemctl disable --now aooserver
systemctl enable canduet-server
systemctl restart canduet-server
sleep 3

if systemctl is-active --quiet canduet-server && ss -ltn | grep -q ":$PORT "; then
    echo "CanDuet relay OK: connection server on $PORT, relay ports $RELAY_PORTS"
    exit 0
fi

echo "CHECK FAILED: canduet-server didn't start; going back to the stock server" >&2
journalctl -u canduet-server -n 20 --no-pager >&2 || true
systemctl disable --now canduet-server || true
systemctl enable --now aooserver
exit 1
