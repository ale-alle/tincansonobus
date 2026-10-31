# Test server (Vultr, Toronto)

A short-lived server for testing CanDuet between two homes: the connection server (stock `essej/aooserver`, until our T-023 fork exists) and LiveKit for video (T-009). Created by one script, destroyed when the test ends.

## Cost
- **Vultr Cloud Compute, Regular, 2 GB RAM / 1 vCPU, Toronto, Ubuntu 24.04 x64** — about US$10/month ❓, billed by the hour up to the monthly cap. A 3-week test ≈ US$7.
- Vultr bills a server until it is **destroyed**; a stopped server still costs money.
- 1 GB (≈ US$5) is enough for audio only or one video lesson at a time.
- Bandwidth: a 1:1 video lesson ≈ 2.7 GB per hour through the server; the plan's 2 TB+ ❓ is far more than a test needs.

## 1. Create it (≈ 20 min)
1. Vultr → **Deploy** → Cloud Compute → Shared CPU → **Regular** (or Regular Performance), location **Toronto**, image **Ubuntu 24.04 LTS x64**, the 2 GB plan.
2. **SSH Keys:** add your public key (`type $env:USERPROFILE\.ssh\id_ed25519.pub`; create one with `ssh-keygen -t ed25519` if missing).
3. **Additional features** → enable **Cloud-Init User-Data** and paste all of [`server/vps/vultr-startup.sh`](../server/vps/vultr-startup.sh). Turn off auto backups (not needed for a test).
4. Hostname `canduet-test` → **Deploy**. Note the server's IPv4 address.
5. At the DNS host for tincanstudios.ca, add two **A** records to that IP: `rtc` and `turn`. On Cloudflare set both to **DNS only** (grey cloud) — the proxy can't carry TURN or WebRTC.
6. Wait ~10 minutes (the first boot builds aooserver), then check on the server:
   ```
   ssh root@<ip>
   tail -n 3 /var/log/canduet-setup.log      # ends with "CanDuet setup done"
   systemctl status aooserver --no-pager     # active (running)
   cd /opt/livekit && docker compose ps      # caddy and livekit "Up"
   cat /root/canduet-test.txt                # values for the test PCs
   ```
7. From your PC: `curl.exe https://rtc.tincanstudios.ca` → `OK`. If it fails with a certificate error, the DNS records were added after Caddy first tried: `ssh root@<ip> "cd /opt/livekit && docker compose restart caddy"` and try again after a minute.

## 2. Test between two homes
Testers need only a zip: no Python, repo or LiveKit secret on their PC. The join service runs on the server.

**Once per server**, from a PC with this repo: create `tickets.env` with one line, `MOCK_TICKETS=<random>=Teacher,<random>=Student` (LF line ending), then:
```powershell
scp scripts\tincan-mock-redeem.py video-page\index.html server\vps\install-test-api.sh tickets.env root@<ip>:/root/canduet-api-setup/   # make the folder first
ssh root@<ip> "bash /root/canduet-api-setup/install-test-api.sh"   # ends with "join service OK"
```
The script restores Caddy's config if the check fails.

**Per tester:** `scripts\make-tester-kit.ps1 -Ticket <ticket> -Role Student -OutDir <folder>` makes a zip with the app, `Join test lesson.bat` and [TESTER-README.txt](TESTER-README.txt). Send it privately (it holds the ticket). Rebuild the kits after rebuilding the app. Windows only: there is no Mac build with video.

To check what each tester sees or change a ticket, edit `tickets.env` and run the script again; the room stays the same.

### Older method: each PC runs its own mock
Each PC needs a CanDuet build from this branch (or later), Python 3, and a copy of this repo (for the mock redeem API and `video-page`). Agree a long random room seed and share it privately, e.g. `python -c "import secrets; print(secrets.token_urlsafe(24))"`.

On **each** PC (PowerShell):
```powershell
# paste the four $env:MOCK_… lines from /root/canduet-test.txt, then:
$env:MOCK_ROOM_SEED = "<the shared seed>"
$env:MOCK_USER_NAME = "Alex"                                        # what the other person sees
python scripts\tincan-mock-redeem.py                                # terminal 1
python -m http.server 8790 --bind 127.0.0.1 --directory video-page  # terminal 2
$env:TINCAN_API_BASE = "http://localhost:8788"                      # terminal 3
$env:TINCAN_VIDEO_PAGE = "http://localhost:8790/"
& $exe "canduet://join?ticket=demo-ok"
```
Both should join "Demo lesson" (audio, encrypted with the shared lesson key) and open the video window.

**Record** (feeds T-008/T-009): each home's internet provider and Wi-Fi vs cable; audio round trip shown in CanDuet; dropouts per 10 minutes; video delay behind audio (clap test); whether video used UDP or fell back to TURN (`docker compose logs livekit | grep -i turn`).

## 3. Destroy it
Vultr → the server → **Destroy** (and delete the `rtc`/`turn` DNS records). Optional: take a snapshot first to bring the same server back later (small monthly charge per GB ❓).

## What this test does and doesn't cover
- **Audio is still peer-to-peer.** The connection server only introduces the two PCs; audio flows directly between them (encrypted, but each side sees the other's IP). The Tin Can relay (D-009 phase 2, T-022/T-023) isn't built yet — fine for adult friends, not for real students.
- **Video** goes through our LiveKit server (SFU), so the two homes never see each other's address for video.
- The LiveKit API secret stays on the server (the older method puts it on both test PCs); it dies with the server.
- Not production: one server, no monitoring, no backups, versions pinned in the script (LiveKit `v1.13.9`, Caddy-L4 `v2.11.3`, aooserver `81a4cf9`).

## Troubleshooting
| Symptom | Likely fix |
|---|---|
| "couldn't reach Tin Can" | Mock not running, or `TINCAN_API_BASE` not set in the terminal that starts CanDuet |
| Joins but never sees the other person | Different `MOCK_ROOM_SEED`, or the old server address: check `MOCK_SERVER` before starting the mock; `systemctl status aooserver` |
| Video window says it can't connect | `curl.exe https://rtc.tincanstudios.ca` (certificate/DNS), `MOCK_VIDEO_KEY`/`SECRET` copied exactly |
| Video works on one network but not another | Strict firewall; check `turn` DNS record and that TURN/TLS on 443 answers |
| Setup log stops early | `cat /var/log/canduet-setup.log`; fix and re-run the failed part by hand, or destroy and deploy again |
