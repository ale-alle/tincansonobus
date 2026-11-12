# The lesson window (T-070)

Stage 1 of the CanDuet redesign (decision D-028 in the tincanstudios-site repo; mockup and spec in `D:\GitHub\canduet-redesign\`). During a lesson the main window shows one web page instead of SonoBus's rows of audio controls.

## What you see
- **People:** the other person's video tile, large, and your own tile, smaller. With three or more people, the tiles share a grid and your own tile takes an empty spot.
- **The red string** joins your tile to the other person's. It's tight and red when connected, loose, grey and dotted while waiting, and broken when the room can't be reached or the other person dropped. With three or more people there's no string, only the label.
- **The label on the string:** "18 ms · great" for teachers, "Great" for learners. Clicking it explains the delay in plain words and offers that person's audio options. Teachers also see the numbers: sound each way, network round trip, smoothing and dropped packets.
- **Tile hover:** pan along the top, volume on the right, **Audio options** at the bottom right. Double-click a slider to reset it.
- **Back of a tile (Audio options):** volume, left/right, Mute, Solo (teachers), Sound quality (Auto / Best for music / Save data), If the sound crackles (Auto / Faster / Smoother), and Advanced for teachers: Reset smoothing, Remove from lesson (press twice), and the formats and numbers.
- **Lesson bar:**
  - Left: the can, the lesson title, the time ("12:04 of 30:00") and a progress line.
  - Mic: mute, plus ▲ for the meter, microphone level, Hear myself, the device, and Reverb… for teachers.
  - Camera: on/off, plus ▲ for the camera and Mirror.
  - Share: your screen or one window, or a second camera.
  - Tools, Board, Chat, and Record (asks everyone first, then a pill with the time, Mark and Stop).
  - Settings: lesson volume, Mute everything I hear, Labels under the icons (teachers), All settings, Set up your sound again, Help.
  - Leave: asks once.
- **Waiting / couldn't connect:** the waiting card has a microphone meter, "Check my sound" and "Tune up while you wait". The problem card has three numbered steps.
- **Shortcuts** (off while typing): M mic, V camera, T tools, B board, C chat, H mark (while recording), Esc closes or flips back.

## How it's built
| Part | Where |
|---|---|
| The page | `video-page/lesson.html`, served by the website at `/app/video/lesson.html` next to the video page. Fonts (Inter, Bricolage Grotesque, OFL) and can drawings sit beside it. |
| The app side | `Source/TinCanLesson.{h,cpp}` (`TinCan::LessonView`): owns the WebView2 page, sends state about 10 times a second, and carries out the page's requests. The message list is in the header. |
| Plain rules | `Source/TinCanLessonRules.{h,cpp}`: what Auto/Faster/Smoother and Auto/Best/Save mean for the engine. Covered by `TinCanTests`. |
| Tools | `TinCan::ToolsPanel` docks into the gap the page leaves for it. Outside a lesson the same panel lives in the Teaching tools window. The tabs are now Tuner, Metronome, Instrument, Summaries (Instrument used to come first), plus Workspace in the window only. |
| Join | `TinCan::parseRedeemResponse` reads the optional `role` and `startsAt`/`endsAt`. Teacher controls go to `role: "teacher"`, or to a server too old to send roles or times. A server that sends times without a role, or an unknown role, gives the learner's controls. So apply the database migration before deploying the rooms-worker. |

WebView2 draws over anything JUCE draws inside the main window. That gives three rules:
- Everything that floats is in the page.
- The docked Tools panel is a borderless child window, created after the web view so it sits on top.
- Native boxes that still exist open as windows of their own: All settings, Reverb…, the "Record this lesson?" question and the sound setup. The app's own pop-up tips become the page's notice at the top.

Nothing was removed from the audio engine. Without WebView2 the app falls back to SonoBus's window, with the "Video needs WebView2" window.

## Not in stage 1
- The Settings panel docked on the right, and EQ/compressor/push-to-talk from the Mic menu: T-072.
- **Can't be reached during a lesson yet** (they were in SonoBus's window, and aren't in All settings): the soundboard, playing a file into the lesson, the per-person latency test, per-person effects (EQ, compressor), send channels, input pan and channel groups. They're planned for Settings › Advanced (T-072). Until then they're available outside a lesson, or in a lesson with `TINCAN_VIDEO_WINDOW=1` (development only).
- The connection: a short network drop keeps the lesson window open and shows "We couldn't reach the lesson room" while CanDuet rejoins by itself; Leave stops the retries.
- Board in the large spot: the board still opens in its own window.
- Practise on your own, computer sound and French: T-073.

## Testing on one PC
```powershell
# separate settings folder, so a test never touches your real CanDuet settings
cmake -G "Visual Studio 17 2022" -A x64 -B build-test "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" "-DTINCAN_DATA_FOLDER=CanDuet-Test"
cmake --build build-test --config Release --target SonoBus_Standalone
# the page, served locally (from the website repo's public/app/video, or video-page here)
python -m http.server 8796 --bind 127.0.0.1 --directory <site>\public\app\video
$env:MOCK_TICKETS = "t1=Ms Rivera:teacher,t2=Mia:learner"; python scripts\tincan-mock-redeem.py
$env:TINCAN_API_BASE = "http://localhost:8788"; $env:TINCAN_LESSON_PAGE = "http://localhost:8796/lesson.html"
& build-test\SonoBus_artefacts\Release\Standalone\CanDuet.exe --multi-instance "canduet://join?ticket=t1"
& build-test\SonoBus_artefacts\Release\Standalone\CanDuet.exe --multi-instance "canduet://join?ticket=t2"
```
For design checks without the app, open `lesson.html#demo=1` in a browser. The options are listed at the top of the file, for example `&people=3&role=learner&state=drop&open=mic`.

## Sharing keys and chord names (T-074)
- **Share my keys** (Tools › Instrument): while on, your MIDI keyboard and the keys you click light up on everyone else's Instrument view, in red with your name. It's off until you switch it on, and switches off when the lesson ends.
- **Show … keys** is on by default, so nobody has to find a setting first. One person's keys show at a time: whoever started first, until they stop or go quiet for 4 seconds. The notice comes once per person per lesson. Untick it to hide them. When someone starts sharing, the others get a notice: "Ms Rivera is sharing their keys. Open Tools, then Instrument, to see them."
- It only lights keys. The sound already comes through the lesson audio, so nothing is played twice.
- **Chord name:** above the keyboard, for the keys you hold ("Am7", "G7/B", "Csus4"), and for the shared keys ("Ms Rivera: G7"). It follows the Flats and transposing settings. Notes that don't make a known chord show no name rather than a wrong one.
- **How it travels:** small text messages on the lesson's private peer channel (`TinCan::NoteWire`, tag `canduet-notes-1`). Notes are sent as they happen, checked 30 times a second, at most 64 notes per message. A snapshot of the held keys goes out once a second, so a lost message never leaves a key stuck. If nothing arrives for 4 seconds, the other person's keys are cleared.
- **Code:**
  - `Source/TinCanNoteShare.{h,cpp}`: one reader for the MIDI input, plus sending and receiving.
  - `NoteWire` in `TinCanNotes`.
  - `Source/TinCanChords.{h,cpp}`.
  - Tests in `tests/TinCanNoteWireTest.cpp` and `tests/TinCanChordsTest.cpp`.
- **Testing on one PC:** set `TINCAN_DEMO_NOTES` (e.g. `A3 C4 E4 G4`) for one copy and another value for the second copy. Tools then opens on Instrument with those keys held and shared.

## The start menu (T-071, D-029)
- **What you see:** outside a lesson, the main window shows the website's own pages: Sign in, My lessons, booking and the teacher pages. They appear in the site's "app mode": CanDuet in the header, Set up your sound and Help, and no marketing menu or footer.
- **Code:** `Source/TinCanHome.{h,cpp}` (`TinCan::HomeView`) loads `<api base>/my-lessons`. `TINCAN_HOME_PAGE` overrides it for development (https, or http on localhost).
- **Joining:** My lessons' Join asks the rooms server for the one-time ticket, as before, and posts `{"t":"join","url":"canduet://join?ticket=…"}`. The app joins straight away. Leaving the lesson brings My lessons back, reloaded.
- **What it may open:** only pages of the site it came from. Every other link opens in the normal browser. A page may ask the app for exactly three things: join, Set up your sound, and opening a Stripe checkout or billing page in the browser.
- **Payments:** Stripe opens in the browser. My lessons reloads when the CanDuet window gets focus again.
- **Signing in:** stays on the website. The app never sees a password. The web view keeps the session in `%APPDATA%\CanDuet\WebView2`.
- **Offline:** if the site can't be reached, a native screen says so, with Try again and Set up your sound.
- **Fallbacks:** without WebView2, or with `TINCAN_DEV_CONNECT=1`, SonoBus's window stays.

## Practice mode (T-075)
- **Getting in:** **Practise on your own**, on My lessons in the app, or **Practise** on the offline screen. Practice mode is the lesson window with nobody else and no connection.
- **What's different:** your camera fills the people area, with no string, connection label, Share, Chat or waiting card. **Leave** becomes **End practice**.
- **Everything else works as in a lesson:** Mic options, Hear myself, Camera, Tools (tuner, metronome, instrument and chord names, summaries), Board, Settings, and Record.
- **Recording on your own:** it starts straight away, with no asking. Highlights work. Summaries are titled "Practice". End practice (or joining a lesson meanwhile) stops and saves a recording that's still going.
- **Works offline:** the page is the app's own copy, next to CanDuet.exe in `lesson\` (`lesson.html` plus `fonts\` and `cans\`). The build's post-build step puts it there; the installer copies it straight from `video-page\`. Real lessons still load the page from the website. `TINCAN_PRACTICE_PAGE` overrides it for development. The folder is in the user's own Programs folder, like CanDuet.exe, so it is no less protected than the app itself.
- **Camera:** comes straight from WebView2 (no video room), so nothing leaves the computer.
- **Ready for lesson modules:** in Practice mode, the app sends the MIDI input's notes and any shared keys to the page (`{"t":"notes","e":[[type,channel,note,value,source],…]}`, in batches of 256). Keys clicked on the screen aren't sent yet. The page checks the values and passes them on as a `canduet-notes` event. That big camera area is meant to host lesson modules later, and they can react to what the student plays.
- **Code:** `LessonInfo::practice` and `getPracticePage()` in `TinCanLesson`, `startPractice()` in the editor, and `body[data-mode=practice]` in `lesson.html`.
