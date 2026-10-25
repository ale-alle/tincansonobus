# In-lesson video (T-009)

Two-way video between teacher and student inside CanDuet (D-002), without slowing the audio. Status: **prototype on branch `t-009-video`** (2026-10-25). Project decisions live in the tincanstudios-site repo; this file is the client-side design.

## Recommendation
**Show the video in a web page inside the app, using Microsoft Edge WebView2, connected to a self-hosted LiveKit server.**

| | Option | Effort | Verdict |
|---|---|---|---|
| A | Native libwebrtc / LiveKit C++ SDK compiled into the app | Weeks: ~1 GB toolchain, our own camera capture and video drawing in JUCE, a GPL check for each library | Too slow for v1.0 (R-021) |
| B | libdatachannel + libvpx/OpenH264 + Media Foundation camera | Weeks: we'd build what a browser already has (bandwidth adaptation, packet loss recovery) | No |
| **C** | **WebView2 window running LiveKit's web client** | **Days. Chromium handles the camera, codecs, adaptation and reconnects** | **Chosen** |
| D | External browser (like SonoBus's VDO.Ninja button) | Hours, but families juggle two windows and a link | Rejected by D-002 |

Why C fits:
- **Still "inside the client" (D-002).** No browser, no link to copy, the window opens on Join.
- **Audio isn't touched.** The page never asks for the microphone (and the server's token forbids publishing one), so lesson audio stays on CanDuet's own low-latency path. Video may run ~150–300 ms ❓ behind the audio, and the audio is never delayed to match it.
- **No IP sharing.** With an SFU each person connects only to our server, never to each other (R-004).
- **Encrypted in transit** by WebRTC (DTLS-SRTP). End-to-end encryption is phase 3 below.
- **Already here.** WebView2 comes with Windows 11 and current Windows 10. JUCE 7.0.8 (in `deps/juce`) supports it.
- **Separate process.** WebView2 runs out of process, so video encoding can't stall the audio thread.
- **Fixable without an app update.** The page is served by our website, so we can fix video bugs without shipping a new app.

## How it works
```
Website My lessons ──Join──> canduet://join?ticket=…
CanDuet ── POST /api/rooms/redeem ──> rooms-worker
        <── {server, group, groupPassword, lessonKey, title, userName,
             video: {url: "wss://video.tincanstudios.ca", token: "<LiveKit JWT>"}}
CanDuet ── audio (AOO, encrypted) ──> connection server / peers      (unchanged)
CanDuet ── VideoWindow (WebView2) ── https://tincanstudios.ca/app/video/#url=…&token=…&name=…
        page ── WebRTC (camera only) ──> LiveKit SFU (Toronto) <── other participant
```
- The address fragment (`#…`) is never sent to any server. The page reads it and removes it from the address straight away (`history.replaceState`).
- The web view can only go to pages on the video page's own site. Links that try to open new windows are ignored.
- If WebView2 is missing, the window says so with Microsoft's download link. The audio lesson carries on.
- Closing the video window keeps the audio going. Leaving the lesson, or losing the server, closes the window.
- A redeem reply with no `video` (or a malformed one) joins audio only, so the server side can be rolled out separately.

### Redeem contract addition (rooms-worker, site repo)
```
"video": {
  "url":   "wss://video.tincanstudios.ca",   // wss only (ws:// accepted for localhost in development)
  "token": "<LiveKit access token>"          // HS256 JWT, base64url characters and dots, ≤ 4096 chars
}
```
Token claims, signed with the LiveKit API secret (a Worker secret; the client never sees it):
```json
{ "iss": "<LIVEKIT_API_KEY>", "sub": "<per-lesson random participant id>", "name": "<userName>",
  "nbf": <lesson start − 15 min>, "exp": <lesson end + 15 min>,
  "video": { "room": "<the lesson's random room id>", "roomJoin": true,
             "canPublish": true, "canSubscribe": true, "canPublishData": false,
             "canPublishSources": ["camera"] } }
```
- Use the same join window as the room ticket (T-019).
- The room name is a random per-lesson value, never the lesson id or anyone's name.
- The identity is random per participant per lesson, so the SFU learns nothing it doesn't need.
- Web Crypto HMAC-SHA256 in the Worker is enough; no SDK is needed. `scripts/tincan-mock-redeem.py` (`video_token`) shows the exact encoding.

### Video page
- `video-page/index.html` is the reference copy. It moves to the website as `public/app/video/index.html` when T-009 is accepted.
- It uses `livekit-client@2.9.1` (Apache-2.0), pinned with a subresource-integrity hash. Before launch, copy that file onto our own site so lessons don't depend on a CDN.
- Features:
  - Remote video large, own camera small and mirrored.
  - Camera on/off and camera picker (both remembered).
  - Plain-language status messages: waiting, camera off, reconnecting, camera blocked.
- 720p capture with simulcast and adaptive stream, so the quality drops on weak connections instead of freezing.
- It exposes `window.canduetVideo` (phase, local/remote video flags) for support and automated tests.

### Build (Windows)
- `TINCAN_VIDEO` (default ON) turns on `JUCE_WEB_BROWSER` + `JUCE_USE_WIN_WEBVIEW2`.
- CMake downloads the WebView2 SDK `1.0.2903.40` from nuget.org (SHA-256 pinned) into the build folder.
- A post-build step copies `WebView2Loader.dll` next to `CanDuet.exe`; `distwin.sh` and `wininstaller.iss` ship it.
- `TINCAN_VIDEO_PAGE` (CMake) or the environment variable of the same name (https, or http://localhost) overrides the page address. The default is `<TINCAN_API_BASE>/app/video/`.
- Code: `Source/TinCanVideo.{h,cpp}`, hooks in `SonobusPluginEditor.cpp` (`handleTinCanURL`, `closeLessonVideo`).

## Server (LiveKit, self-hosted)
- **LiveKit server** (Apache-2.0): a single Go binary with built-in TURN. It runs on the same Toronto VPS as the connection server (T-023), so it is part of that cost decision.
- **Ports:** 443/TCP (signalling + TURN/TLS through strict firewalls), 7881/TCP, and a UDP range for media.
- **Bandwidth, rough ❓:**
  - About 1.5 Mbps each way per person at 720p.
  - A 1:1 lesson moves about 3 Mbps in and 3 Mbps out through the SFU, roughly 2.7 GB per lesson hour.
  - A VPS with 1–2 TB/month covers about 400–700 lesson hours. Measure in T-008/T-009 (R-012).
- **Data stays in Canada** (D-012): the SFU forwards media and stores nothing; nothing is recorded.
- **LiveKit Cloud** would avoid running a server, but it doesn't promise a Canadian region. Keep it as a fallback only.

## Phases
1. **Prototype (this branch).**
   - Page + app window + mock tokens.
   - ✅ Page verified with two headless Edge sessions (fake cameras) through a local `livekit-server --dev`: both receive each other's video and the token is removed from the address.
   - ✅ App verified 2026-10-25: Release x64 build; two CanDuet copies joined via `canduet://join?ticket=demo-ok` each opened the video window and received the other's video. WebView2 was started with fake cameras and a debugging port (`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS`) to read the page state.
   - Not yet tried: a real webcam and the WebView2 camera prompt, two PCs, the "WebView2 missing" message.
2. **Real lessons.**
   - rooms-worker issues `video` tokens.
   - Host the page on the site, with a Content-Security-Policy for that path.
   - LiveKit on the Toronto VPS with TLS.
   - Owner test on two PCs; measure glass-to-glass latency and bandwidth.
3. **Polish.**
   - Dock the video inside the main window (option) and remember the window size and position.
   - Grant the camera only to our site, without the generic WebView2 prompt (needs a small patch to JUCE's `WebView2` class: `add_PermissionRequested`).
   - Turn on LiveKit end-to-end encryption with a key derived from the lesson key (HKDF, label "canduet-video"), so even our SFU can't see the video.
   - A setup-check page with a camera preview before the lesson.
4. **Later:** screen or second-camera share (sheet music, hands at the keyboard), macOS (WKWebView).

## Open questions for the owner
- Hosting LiveKit on the same Toronto VPS as the audio server (recommended) makes the VPS decision cover both.
- Separate video window first (built) vs. docked in the main window: the separate window suits teachers with two screens; docking can come in phase 3.
- Licences, to add to the ARCHITECTURE table when convenient:
  - LiveKit server and client: Apache-2.0 (compatible with GPLv3).
  - WebView2 SDK/loader: Microsoft's BSD-style SDK licence, redistributable.
  - The Edge WebView2 runtime is part of Windows.
