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
| Tools | `TinCan::ToolsPanel` (Tuner, Metronome, Instrument, Summaries) docks into the gap the page leaves for it. Outside a lesson the same panel lives in the Teaching tools window. |
| Join | `TinCan::parseRedeemResponse` reads the optional `role` and `startsAt`/`endsAt`. Older servers send neither: the person then gets the teacher's controls and a plain elapsed time. |

WebView2 draws over anything JUCE draws inside the main window. That gives three rules:
- Everything that floats is in the page.
- The docked Tools panel is a borderless child window, created after the web view so it sits on top.
- Native boxes that still exist open as windows of their own: All settings, Reverb…, the "Record this lesson?" question and the sound setup. The app's own pop-up tips become the page's notice at the top.

Nothing was removed from the audio engine. Without WebView2 the app falls back to SonoBus's window, with the "Video needs WebView2" window.

## Not in stage 1
- The Settings panel docked on the right, and EQ/compressor/push-to-talk from the Mic menu: T-072. They're still reachable through All settings and SonoBus's window, which `TINCAN_VIDEO_WINDOW=1` brings back for development.
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
