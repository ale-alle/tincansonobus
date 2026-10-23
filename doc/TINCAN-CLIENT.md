# Tin Can client changes

What the fork adds on top of SonoBus 1.7.2. Project tasks and decisions live in the tincanstudios-site repo (`docs/TASKS.md`, `docs/DECISIONS.md`).

## Join links (T-018, client side)
The website's **Join lesson** button opens `canduet://join?ticket=<opaque>`.

1. Windows starts `CanDuet.exe "canduet://join?ticket=…"` (registered by `release/wininstaller.iss`). If the app is already running, the link is handed to it — the app is now single-instance; `--multi-instance` turns that off for testing.
2. The app redeems the ticket with the app server and joins the room. Group name, password and lesson key are never shown, saved to "recents", or put into share links; the room appears as the lesson title (or "Lesson room").
3. Errors show a short message pointing back to "My lessons". The ticket is never logged.

### Redeem API (contract for T-019)
```
POST {TINCAN_API_BASE}/api/rooms/redeem
Content-Type: application/json
{"ticket": "<ticket>"}

200 {"server": "host[:port]", "group": "…", "groupPassword": "…",
     "lessonKey": "<base64 of 32 random bytes>", "title": "Piano with Sam"}   // title optional
4xx  ticket expired / used / not for this user  → "link has expired" message
5xx or no connection                            → "couldn't reach Tin Can" message
```
- Tickets: URL-safe characters only (`A–Z a–z 0–9 - _ . ~`), ≤ 4096 chars, short-lived, single-user (D-008).
- `TINCAN_API_BASE` is a CMake option (default `https://tincanstudios.ca`); the environment variable of the same name overrides it for development and only accepts `https://…` or `http://localhost…`. Redirects are not followed.
- The server should issue the **same** `group`, `groupPassword` and `lessonKey` to every participant of a lesson and a fresh set per lesson.
- Not built yet: sign-in inside the app, "My lessons", the waiting-room screen and auto-reconnect UI (ARCHITECTURE "Client changes in the fork").

## Per-lesson encryption (D-009 phase 1)
With a lesson key set, every UDP packet between participants is sealed with XChaCha20-Poly1305 (Monocypher 4.0.2, vendored in `deps/monocypher`, BSD-2/CC0):

```
"TCE1" | nonce (24) | MAC (16) | ciphertext        44 bytes overhead per packet
nonce = 16 random bytes per key (OS CSPRNG) + 64-bit counter
```
- Covers audio, peer handshakes/pings and SonoBus peer messages (chat, peer info) — everything except traffic to the connection server (`/aoo/server/*` out, `/aoo/client/*` in), which stays plaintext because the server holds no key.
- While a key is set, unsealed peer packets and packets that fail authentication are dropped (`getDroppedCipherPacketCount()`). No plaintext fallback.
- Code: `Source/TinCanPacketCipher.{h,cpp}`; hooks in `endpoint_send`, `client_send` and `doReceiveData` in `Source/SonobusPluginProcessor.cpp`.
- Tests: `TinCanTests` target (`tests/TinCanPacketCipherTest.cpp`).

### Known limits — review before real students (backend-security + qa-reviewer)
- **No replay protection.** A captured packet can be re-sent; AOO's sequence numbers discard most audio duplicates, but control messages are not checked. Fix: per-sender sliding window on the nonce counter.
- **No forward secrecy / key rotation** within a lesson; the key's secrecy rests on HTTPS and the app server.
- **IP addresses are still shared** peer-to-peer and **metadata** (who talks to whom, packet sizes and timing) is visible. The relay (D-009 phase 2, T-022/T-023) is still required before lessons with strangers or minors.
- The connection-server traffic (user name, group name, MD5 of group password) is unencrypted — use per-lesson random values so it reveals nothing useful.

## Connection server (T-023, client side)
`TINCAN_SERVER_HOST` / `TINCAN_SERVER_PORT` CMake options set the default server. Empty keeps `aoo.sonobus.net:10998` until our server exists.

## Testing without the web platform
`scripts/tincan-mock-redeem.py` stands in for the redeem API; `-k|--lesson-key <base64>` sets a key from the command line. Steps in `BUILD-WINDOWS.md` §3b.
