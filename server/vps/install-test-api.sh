#!/bin/bash
# Adds the test join service to the CanDuet test server, so testers need only the app (no Python,
# no repo, no LiveKit secret on their PC). Serves https://<rtc domain>:8443: POST /api/rooms/redeem
# (scripts/tincan-mock-redeem.py) and GET /app/video/ (video-page/index.html).
# Run as root on the server, from a folder holding tincan-mock-redeem.py, index.html and
# tickets.env (one line: MOCK_TICKETS=<ticket>=<name>,<ticket>=<name>). Safe to run again.
# Steps: doc/TINCAN-VPS.md.
set -euo pipefail

SRC=$(cd "$(dirname "$0")" && pwd)
APP_DIR=/opt/canduet-test-api
ENV_FILE=/etc/canduet-test-api.env
CADDY=/opt/livekit/caddy.yaml
PORT=8443

value() { sed -n "s/^\$env:$1 *= *\"\(.*\)\"\$/\1/p" /root/canduet-test.txt; }
RTC_URL=$(value MOCK_VIDEO_URL)
RTC_DOMAIN=${RTC_URL#wss://}

install -d "$APP_DIR"
install -m 644 "$SRC/tincan-mock-redeem.py" "$APP_DIR/redeem.py"
install -m 644 "$SRC/index.html" "$APP_DIR/index.html"

umask 077
{
    cat "$SRC/tickets.env"
    echo "MOCK_SERVER=$(value MOCK_SERVER)"
    echo "MOCK_VIDEO_URL=$RTC_URL"
    echo "MOCK_VIDEO_KEY=$(value MOCK_VIDEO_KEY)"
    echo "MOCK_VIDEO_SECRET=$(value MOCK_VIDEO_SECRET)"
    echo "MOCK_VIDEO_TTL=86400"
    echo "MOCK_VIDEO_PAGE=$APP_DIR/index.html"
    # keeps the room and lesson key the same across restarts
    grep -s '^MOCK_ROOM_SEED=' "$ENV_FILE" || echo "MOCK_ROOM_SEED=$(openssl rand -hex 24)"
} > "$ENV_FILE.new"
mv "$ENV_FILE.new" "$ENV_FILE"
umask 022

cat > /etc/systemd/system/canduet-test-api.service <<EOF
[Unit]
Description=CanDuet test join service
After=network-online.target
Wants=network-online.target

[Service]
EnvironmentFile=$ENV_FILE
ExecStart=/usr/bin/python3 $APP_DIR/redeem.py
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
systemctl daemon-reload
systemctl enable canduet-test-api
systemctl restart canduet-test-api

ufw allow "$PORT/tcp"

# Caddy ends TLS on the new port with the certificate it already holds for the rtc domain.
CADDY_CHANGED=0
if ! grep -q 'test_api:' "$CADDY"; then
    cp "$CADDY" "$CADDY.before-test-api"
    sed -i "/^    servers:\$/a\\
      test_api:\\
        listen: [\":$PORT\"]\\
        routes:\\
          - match:\\
              - tls:\\
                  sni: [\"$RTC_DOMAIN\"]\\
            handle:\\
              - handler: tls\\
                connection_policies:\\
                  - alpn: [\"http/1.1\"]\\
              - handler: proxy\\
                upstreams:\\
                  - dial: [\"localhost:8788\"]" "$CADDY"
    CADDY_CHANGED=1
    (cd /opt/livekit && docker compose restart caddy)
fi

for _ in $(seq 1 15); do
    if [ "$(curl -s -o /dev/null -w '%{http_code}' "https://$RTC_DOMAIN:$PORT/app/video/")" = "200" ] \
        && [ "$(curl -s -o /dev/null -w '%{http_code}' "https://$RTC_DOMAIN")" = "200" ]; then
        echo "CanDuet test join service OK: https://$RTC_DOMAIN:$PORT"
        exit 0
    fi
    sleep 2
done

echo "CHECK FAILED: the join service or LiveKit did not answer over HTTPS" >&2
if [ "$CADDY_CHANGED" = 1 ]; then
    mv "$CADDY.before-test-api" "$CADDY"
    (cd /opt/livekit && docker compose restart caddy)
    echo "Caddy config restored; LiveKit is back as it was" >&2
fi
exit 1
