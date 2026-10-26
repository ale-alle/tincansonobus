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
     "lessonKey": "<base64 of 32 random bytes>", "title": "Lesson with Sam",
     "userName": "Ava"}   // title and userName optional; userName = name shown to the other person (the child for a parent)
4xx  ticket expired / used / not for this user  → "link has expired" message
5xx or no connection                            → "couldn't reach Tin Can" message
```
- Tickets: URL-safe characters only (`A–Z a–z 0–9 - _ . ~`), ≤ 4096 chars, short-lived, single-user (D-008).
- `TINCAN_API_BASE` is a CMake option (default `https://tincanstudios.ca`); the environment variable of the same name overrides it for development and only accepts `https://…` or `http://localhost…`. Redirects are not followed.
- The server should issue the **same** `group`, `groupPassword` and `lessonKey` to every participant of a lesson and a fresh set per lesson.
- Not built yet: sign-in inside the app, "My lessons", the waiting-room screen and auto-reconnect UI (ARCHITECTURE "Client changes in the fork").

## In-lesson video (T-009)
The redeem reply may include `"video": {"url", "token"}`; the app then opens a video window (WebView2) showing the website's video page connected to our LiveKit server. Audio is unchanged. Design, contract and test status: [TINCAN-VIDEO.md](TINCAN-VIDEO.md).

## Per-lesson encryption (D-009 phase 1)
With a lesson key set, every UDP packet between participants is sealed with XChaCha20-Poly1305 (Monocypher 4.0.2, vendored in `deps/monocypher`, BSD-2/CC0):

```
"TCE1" | nonce (24) | MAC (16) | ciphertext        44 bytes overhead per packet
nonce = 16 random bytes per key (OS CSPRNG) + 64-bit counter
```
- Covers audio, peer handshakes/pings and SonoBus peer messages (chat, peer info) — everything except traffic to the connection server (`/aoo/server/*` out, `/aoo/client/*` in), which stays plaintext because the server holds no key.
- While a key is set, unsealed peer packets and packets that fail authentication are dropped (`getDroppedCipherPacketCount()`). No plaintext fallback.
- Code: `Source/TinCanPacketCipher.{h,cpp}`; hooks in `endpoint_send`, `client_send` and `doReceiveData` in `Source/SonobusPluginProcessor.cpp`.
- Tests: `TinCanTests` target (`tests/TinCanPacketCipherTest.cpp`; all Tin Can tests run from `tests/TinCanTestMain.cpp`).

### Known limits — review before real students (backend-security + qa-reviewer)
- **No replay protection.** A captured packet can be re-sent; AOO's sequence numbers discard most audio duplicates, but control messages are not checked. Fix: per-sender sliding window on the nonce counter.
- **No forward secrecy / key rotation** within a lesson; the key's secrecy rests on HTTPS and the app server.
- **IP addresses are still shared** peer-to-peer and **metadata** (who talks to whom, packet sizes and timing) is visible. The relay (D-009 phase 2, T-022/T-023) is still required before lessons with strangers or minors.
- The connection-server traffic (user name, group name, MD5 of group password) is unencrypted — use per-lesson random values so it reveals nothing useful.

## Connection server (T-023, client side)
`TINCAN_SERVER_HOST` / `TINCAN_SERVER_PORT` CMake options set the default server. Empty keeps `aoo.sonobus.net:10998` until our server exists.
For testing between two homes, a throwaway Toronto server (stock aooserver + LiveKit) is described in [TINCAN-VPS.md](TINCAN-VPS.md).

## Teaching tools (T-034 onwards)
**Tools** in the bottom toolbar (next to the metronome) opens the *Teaching tools* window; click again or close it to put it away. A separate window so the lesson controls stay visible. Spec: `.claude/worktrees/asiosdk/CanDuet_Features.md`; tasks T-034–T-038 in the site repo.

### On-screen instrument (T-034)
- Piano keyboard showing notes from the MIDI input (blue) and notes clicked with the mouse (orange); notes kept sounding only by the sustain pedal are pale. 25/37/49/61/88 keys, octave buttons, note names on C only or every key, sharps or flats, middle C = C4 or C3. Note names are concert pitch.
- **MIDI input:** Settings > Audio now lists *Active MIDI inputs* (the standard JUCE list, saved with the audio settings; it was hidden in SonoBus). MIDI from another program needs a loopback port (e.g. loopMIDI, or the Windows MIDI Services loopback) turned on in that list.
- **Threads:** `processBlock` copies note-on/off, sustain (CC 64), all-sound-off (CC 120), reset-controllers (CC 121) and all-notes-off (CC 123) into a fixed 1024-entry lock-free queue — no allocation, no locks, and only while the window is open. The window drains it 30 times a second on the message thread and repaints only when something changed. If the window falls behind, extra events are dropped, never blocked on. MIDI passing through the plugin is unchanged.
- Mouse notes are local only: they make no sound and aren't sent to the other person (sync comes with T-037's message channel).
- Code: `Source/TinCanNotes.{h,cpp}` (note model, MIDI decoding, names, queue — no JUCE), `Source/TinCanTools.{h,cpp}` (window, panel, keyboard view built on JUCE `MidiKeyboardComponent`). Tests: `tests/TinCanNotesTest.cpp`.
- **Manual test (needs a MIDI keyboard):** plug it in, Settings > Audio > tick it under *Active MIDI inputs*, open Tools: keys light blue while held; chords; repeated notes; hold the pedal and release keys → pale until the pedal comes up; unplug while holding a key → the key clears. Not done yet (no MIDI device connected during development).
- Not yet: settings (range, names) aren't remembered between runs.

### Guitar and bass (T-035)
- The instrument menu switches between piano, guitar (6 strings, 22 frets), bass (4 strings, 20 frets) and bass (5 strings, 21 frets). Only the view changes; the note state and the MIDI input carry on.
- Fretboards show every place a sounding note can be played, highest string at the top, with real fret spacing. Clicking a string at a fret plays that note (orange).
- Tunings are data (`getInstruments()` in `Source/TinCanInstruments.cpp`): guitar standard, drop D, half step down, DADGAD, open G, open D; bass standard, drop D, half step down; 5-string standard. The text field takes any tuning of 1-12 strings, lowest first (`B1 E2 A2 D3 G3 B3 E4` for a 7-string). It turns red while invalid and applies on Enter; typing a preset's notes selects that preset.
- **Note names** menu: concert pitch, or written pitch for Bb, Bb + octave, Eb, Eb + octave, F, A and octave (guitar/bass) instruments. Every label in the view then shows written pitch, and the top right shows the last note as "Written D4  Concert C4". The tuning field is always concert pitch.
- Tests: `tests/TinCanInstrumentsTest.cpp` (note→fret for standard, drop D, DADGAD, 7-string, 4- and 5-string bass, fret limits; note and tuning parsing).
- **Development aid:** `TINCAN_DEMO_NOTES="C4 E4 G4"` (environment variable) shows those notes as held on the MIDI input when the Tools window opens — used to check the views without a MIDI keyboard.

## Testing without the web platform
`scripts/tincan-mock-redeem.py` stands in for the redeem API; `-k|--lesson-key <base64>` sets a key from the command line. Steps in `BUILD-WINDOWS.md` §3b.
