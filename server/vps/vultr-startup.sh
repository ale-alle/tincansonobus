#!/bin/bash
# CanDuet test server (T-023 / T-009): connection server (aooserver) + LiveKit behind Caddy.
# Vultr, Ubuntu 24.04 x64: paste this whole file into "Cloud-Init User-Data" when deploying.
# Runs once at first boot (~10 min); log in /var/log/canduet-setup.log.
# Steps and checks: doc/TINCAN-VPS.md.
set -euxo pipefail
exec > >(tee -a /var/log/canduet-setup.log) 2>&1

# --- edit if needed -------------------------------------------------------------------------
RTC_DOMAIN=rtc.tincanstudios.ca      # LiveKit signalling (wss://), also used for the audio server
TURN_DOMAIN=turn.tincanstudios.ca    # LiveKit TURN over TLS (shares port 443 with RTC_DOMAIN)
AOO_PORT=10998
LIVEKIT_IMAGE=livekit/livekit-server:v1.13.9
CADDY_IMAGE=livekit/caddyl4:v2.11.3  # Caddy with the layer4 module, as LiveKit's generator uses
AOOSERVER_COMMIT=81a4cf94cfc0b25a7efa8fdbb4d53a0ecf00f10b   # essej/aooserver master, 2023-11-27
# ---------------------------------------------------------------------------------------------

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates curl git openssl ufw build-essential pkg-config \
    libcurl4-openssl-dev docker.io docker-compose-v2

# Firewall: SSH, TLS issuance, HTTPS + TURN/TLS, WebRTC TCP, TURN/UDP, WebRTC UDP, aooserver
ufw default deny incoming
ufw default allow outgoing
ufw allow 22/tcp
ufw allow 80/tcp
ufw allow 443/tcp
ufw allow 7881/tcp
ufw allow 3478/udp
ufw allow 50000:60000/udp
ufw allow ${AOO_PORT}/tcp
ufw allow ${AOO_PORT}/udp
ufw --force enable

# --- connection server -----------------------------------------------------------------------
git clone https://github.com/essej/aooserver.git /opt/aooserver-src
git -C /opt/aooserver-src checkout --detach "$AOOSERVER_COMMIT"
make -C /opt/aooserver-src/Builds/LinuxMakefile CONFIG=Release -j"$(nproc)"
install -m 755 /opt/aooserver-src/Builds/LinuxMakefile/build/aooserver /usr/local/bin/aooserver
id aooserver >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin aooserver

cat > /etc/systemd/system/aooserver.service <<EOF
[Unit]
Description=CanDuet connection server (aooserver)
After=network-online.target
Wants=network-online.target

[Service]
User=aooserver
ExecStart=/usr/local/bin/aooserver -p ${AOO_PORT}
Restart=always
RestartSec=2
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now aooserver

# --- LiveKit + Caddy -------------------------------------------------------------------------
mkdir -p /opt/livekit/caddy_data
cd /opt/livekit
PUBLIC_IP=$(ip -4 route get 1.1.1.1 | awk '{for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1)}')
API_KEY="API$(openssl rand -hex 6)"
API_SECRET="$(openssl rand -hex 32)"

cat > livekit.yaml <<EOF
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
  ${API_KEY}: ${API_SECRET}
logging:
  level: info
EOF

# Port 443 is shared by SNI: TURN_DOMAIN -> LiveKit's TURN (5349), RTC_DOMAIN -> signalling (7880).
# Certificates come from Let's Encrypt over port 80 once both names point at this server.
cat > caddy.yaml <<EOF
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
                  - dial: ["localhost:7880"]
EOF

cat > docker-compose.yaml <<EOF
services:
  caddy:
    image: ${CADDY_IMAGE}
    command: run --config /etc/caddy.yaml --adapter yaml
    restart: unless-stopped
    network_mode: host
    volumes:
      - ./caddy.yaml:/etc/caddy.yaml
      - ./caddy_data:/data
  livekit:
    image: ${LIVEKIT_IMAGE}
    command: --config /etc/livekit.yaml
    restart: unless-stopped
    network_mode: host
    volumes:
      - ./livekit.yaml:/etc/livekit.yaml
EOF
chmod 600 livekit.yaml
systemctl enable --now docker
docker compose up -d

# --- values for the test PCs (root only) -----------------------------------------------------
cat > /root/canduet-test.txt <<EOF
PowerShell, on each test PC (keep the secret private; destroy the server when the test ends):
\$env:MOCK_SERVER       = "${RTC_DOMAIN}:${AOO_PORT}"
\$env:MOCK_VIDEO_URL    = "wss://${RTC_DOMAIN}"
\$env:MOCK_VIDEO_KEY    = "${API_KEY}"
\$env:MOCK_VIDEO_SECRET = "${API_SECRET}"
Server IP: ${PUBLIC_IP}
EOF
chmod 600 /root/canduet-test.txt
echo "CanDuet setup done"
