# CanDuet media server in Docker (T-063, D-025 stage 1)

One media server = three containers started by `server/docker/compose.yaml`:

| Container | Image | What it does |
|---|---|---|
| `canduet-server` | ours, `server/docker/Dockerfile` | connection server (TCP/UDP 10998) + lesson audio relay (UDP 40000-40999), doc/TINCAN-RELAY.md |
| `livekit` | `livekit/livekit-server` (pinned) | video, WebRTC UDP 50000-60000, TURN |
| `caddy` | `livekit/caddyl4` (pinned) | certificates; port 443 split by name between LiveKit signalling and TURN/TLS |
| `test-api` (optional) | `python` (pinned) | test join service, https://&lt;rtc domain&gt;:8443, until the rooms-worker hands out real tickets |

All use host networking: media needs wide UDP ranges and the server's real address, and Docker's
port publishing would add a proxy hop per packet. This replaces `server/vps/vultr-startup.sh` +
`install-relay.sh` + `install-test-api.sh` (kept until a server has run from Compose).

## The image

- **CI:** `.github/workflows/canduet-server-image.yml` builds on every push (any branch) that touches
  the server or AOO, checks the image refuses to run without `--relay-ip`, and only then publishes
  `ghcr.io/ale-alle/canduet-server:<commit>` (from `main` also `:main`). Servers use a `:<commit>` tag
  in `.env`, never `:main`.
- **By hand** (any machine with Docker, from the repo root):
  `docker build -f server/docker/Dockerfile -t canduet-server .`
- The build compiles with `server/relay/build.sh` (same files and flags as `install-relay.sh`) and the
  final image is Debian slim + one binary, running as `nobody`, read-only, no capabilities.
- The registry package is private while the repo is. A server pulling it needs
  `docker login ghcr.io` with a GitHub token that has only `read:packages`; or build on the server
  from a full checkout: `docker compose -f compose.yaml -f compose.build.yaml build canduet-server`
  (a copied `server/docker/` folder alone can't build).

## New server (Ubuntu 24.04)

1. Install Docker: `apt-get install -y docker.io docker-compose-v2 ufw openssl`.
2. Copy `server/docker/` to `/opt/canduet` (for the test join service also copy
   `scripts/tincan-mock-redeem.py` → `test-api/redeem.py` and `video-page/index.html` → `test-api/index.html`).
3. `cp env.example .env`, set the two domain names and `CANDUET_SERVER_IMAGE` (a `:<commit>` tag CI
   published from `main`), and `TEST_API=1` + `MOCK_TICKETS` for testing.
4. `bash configure.sh` — fills the public IP, LiveKit secret and lesson-group secret (`GROUP_SECRET`)
   into `.env`, writes `generated/`, opens the firewall. Copy `GROUP_SECRET` into the rooms-worker:
   `wrangler secret put RELAY_GROUP_SECRET` (T-048: the server only creates groups it signed).
5. Point the DNS records at the server, then `docker compose up -d`
   (`docker compose --profile test-api up -d` with the test join service, **testing only**: it hands
   out signed groups to anyone with a test ticket, so it stays off once real lessons run there).
6. Check: `docker compose ps` (all Up), `curl https://<rtc domain>` → `OK`,
   `docker compose logs canduet-server` → "connection server on 10998".

## Updating

Set the new `:<commit>` in `.env`, then `docker compose pull && docker compose up -d` (restarts
only containers whose image changed). No healthchecks yet: a hung container is caught by T-065.
**Restarting `canduet-server` or `livekit` drops the lessons on that server**: update between
lessons until draining (D-025 stage 2, T-064) lets a server finish its lessons first.
Roll back by setting the previous `:<commit>` tag in `.env`.

## Next (D-025)

- **Stage 2 (T-064):** several servers built from this folder; the rooms-worker picks one per lesson
  from a `media_servers` table and puts it in the ticket (the app already uses the ticket's server
  and video URL); servers report load; *draining* for upgrades; signed group names so only ticket
  holders get in.
- **Monitoring (T-065):** Prometheus + Grafana; alerts at 60–70% of bandwidth, CPU or monthly transfer.
- **Stage 3:** create and remove servers through the provider's API from this same folder;
  Kubernetes (Agones, LiveKit multi-node) only at 10–20+ servers.
