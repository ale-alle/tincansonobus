#!/bin/bash
# Prepares this folder to run a CanDuet media server (D-025, doc/TINCAN-DOCKER.md). Safe to run again.
# Fills the blanks in .env (public IP, LiveKit key and secret, room seed), writes generated/livekit.yaml
# and generated/caddy.yaml, and opens the firewall. Run as root, then: docker compose up -d
set -euo pipefail
cd "$(dirname "$0")"

[ -f .env ] || { echo "copy env.example to .env and edit it first" >&2; exit 1; }
chmod 600 .env

# Write KEY=value into .env, replacing an existing line
set_env() {
    if grep -q "^$1=" .env; then sed -i "s|^$1=.*|$1=$2|" .env; else echo "$1=$2" >> .env; fi
}
# Read KEY from .env (value up to the first space or #)
get_env() { sed -n "s/^$1=\([^ #]*\).*/\1/p" .env | tail -n 1; }

[ -n "$(get_env PUBLIC_IP)" ] || set_env PUBLIC_IP \
    "$(ip -4 route get 1.1.1.1 | awk '{for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1)}')"
[ -n "$(get_env LIVEKIT_API_KEY)" ] || set_env LIVEKIT_API_KEY "API$(openssl rand -hex 6)"
[ -n "$(get_env LIVEKIT_API_SECRET)" ] || set_env LIVEKIT_API_SECRET "$(openssl rand -hex 32)"
[ -n "$(get_env MOCK_ROOM_SEED)" ] || set_env MOCK_ROOM_SEED "$(openssl rand -hex 24)"

RTC_DOMAIN=$(get_env RTC_DOMAIN)
TURN_DOMAIN=$(get_env TURN_DOMAIN)
PUBLIC_IP=$(get_env PUBLIC_IP)
AOO_PORT=$(get_env AOO_PORT); AOO_PORT=${AOO_PORT:-10998}
RELAY_PORT_FIRST=$(get_env RELAY_PORT_FIRST); RELAY_PORT_FIRST=${RELAY_PORT_FIRST:-40000}
RELAY_PORT_LAST=$(get_env RELAY_PORT_LAST); RELAY_PORT_LAST=${RELAY_PORT_LAST:-40999}
TEST_API=$(get_env TEST_API)
: "${RTC_DOMAIN:?RTC_DOMAIN missing in .env}" "${TURN_DOMAIN:?TURN_DOMAIN missing in .env}" "${PUBLIC_IP:?no public IP}"

mkdir -p generated caddy_data
umask 077
cat > generated/livekit.yaml <<YAML
port: 7880
bind_addresses:
  - ""
rtc:
  tcp_port: 7881
  port_range_start: 50000
  port_range_end: 60000
  use_external_ip: true
  enable_loopback_candidate: false
turn:
  enabled: true
  domain: ${TURN_DOMAIN}
  tls_port: 5349
  udp_port: 3478
  external_tls: true
keys:
  $(get_env LIVEKIT_API_KEY): $(get_env LIVEKIT_API_SECRET)
logging:
  level: info
YAML
umask 022

# Port 443 is shared by SNI: TURN_DOMAIN -> LiveKit's TURN (5349), RTC_DOMAIN -> signalling (7880).
# With TEST_API=1, port 8443 ends TLS for the test join service (127.0.0.1:8788).
TEST_API_SERVER=""
if [ "$TEST_API" = 1 ]; then
    TEST_API_SERVER="
      test_api:
        listen: [\":8443\"]
        routes:
          - match:
              - tls:
                  sni: [\"${RTC_DOMAIN}\"]
            handle:
              - handler: tls
                connection_policies:
                  - alpn: [\"http/1.1\"]
              - handler: proxy
                upstreams:
                  - dial: [\"localhost:8788\"]"
fi
cat > generated/caddy.yaml <<YAML
logging:
  logs:
    default:
      level: INFO
storage:
  module: file_system
  root: /data
apps:
  tls:
    certificates:
      automate:
        - ${RTC_DOMAIN}
        - ${TURN_DOMAIN}
  layer4:
    servers:
      main:
        listen: [":443"]
        routes:
          - match:
              - tls:
                  sni: ["${TURN_DOMAIN}"]
            handle:
              - handler: tls
              - handler: proxy
                upstreams:
                  - dial: ["${PUBLIC_IP}:5349"]
          - match:
              - tls:
                  sni: ["${RTC_DOMAIN}"]
            handle:
              - handler: tls
                connection_policies:
                  - alpn: ["http/1.1"]
              - handler: proxy
                upstreams:
                  - dial: ["localhost:7880"]${TEST_API_SERVER}
YAML

# Firewall: SSH, TLS issuance, HTTPS + TURN/TLS, WebRTC TCP, TURN/UDP, WebRTC UDP,
# connection server, audio relay. (Host networking: Docker doesn't add its own rules.)
if command -v ufw >/dev/null; then
    ufw default deny incoming
    ufw default allow outgoing
    for rule in 22/tcp 80/tcp 443/tcp 7881/tcp 3478/udp 50000:60000/udp \
                "$AOO_PORT/tcp" "$AOO_PORT/udp" "$RELAY_PORT_FIRST:$RELAY_PORT_LAST/udp"; do
        ufw allow "$rule" >/dev/null
    done
    if [ "$TEST_API" = 1 ]; then ufw allow 8443/tcp >/dev/null; else ufw delete allow 8443/tcp >/dev/null 2>&1 || true; fi
    ufw --force enable >/dev/null
fi

echo "Ready. Start with: docker compose up -d$([ "$TEST_API" = 1 ] && echo '   (test join service: docker compose --profile test-api up -d)')"
echo "Public IP ${PUBLIC_IP}; ${RTC_DOMAIN} and ${TURN_DOMAIN} must point here before Caddy can get certificates."
