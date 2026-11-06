# Lesson recording, highlights and summaries (T-045, T-046)

Status: **built 2026-11-05 on branch `t-044-share-record`**, tested with two copies on one PC (below), and **verified by the owner the same day in a lesson with the student between two homes** (recording and summaries worked). Project tasks and decisions live in the tincanstudios-site repo (`docs/TASKS.md`, `docs/APP_TOOLS.md`).

## What people see
- **Record** (the existing button) asks everyone else in the lesson first: "Alex wants to record this lesson… **Allow recording** / **Not now**".
- Nothing is recorded until everyone has said yes. Then **every computer records its own copy** with SonoBus's recorder (default: one FLAC file of the mix, in Music\CanDuet).
- While it records, a **Mark** button sits next to Record. Mark adds a highlight at that moment; a small box lets you name it (optional). Highlights are shared, so both copies get the same list.
- **Anyone can stop it, and stopping stops it for everyone.** Pressing Record while waiting for answers cancels the question.
- When it stops, each computer saves a **summary** next to its recording and opens **Tools › Summaries**: lesson title, date, length, who was there, and the highlights. Double-click a highlight (or **Play highlight**) to play the recording from 2 seconds before it. Playback uses the main window's file player, so a teacher can also replay a moment to the student with "Send File Playback to All".
- **Delete…** moves a recording and its summary to the Recycle Bin (only this computer's copy). The player lets go of the file first; if the recording can't be moved, the summary is kept and a message says so.
- The Summaries window opens by itself only after recordings with other people; solo recordings just appear in the tab.
- Practising alone (nobody else connected): Record starts straight away, Mark still works.

## Rules (consent)
| Situation | Result |
|---|---|
| Someone says Not now | Nothing is recorded; the asker is told who said no |
| Nobody answers within 60 s | The question is cancelled |
| Someone leaves before answering | The question is cancelled (or, in a group, they no longer need to answer) |
| Someone joins while the question is open | The question is cancelled; ask again |
| Someone who hadn't agreed joins during a recording | Recording stops on every computer, with a message |
| Someone who agreed leaves, quits or drops out | Recording stops on every computer; each keeps its copy so far. Coming back needs a new yes |
| A Stop message gets lost | The next "still recording" check (every 10 s) gets a Stop back, so it stops within about 10 s |
| Nothing heard from someone for 30 s | Recording stops ("lost contact with …") |
| A question or yes is never followed up (lost message) | It ends after 75 s; asking again with a new request replaces it |
| Both press Record at once | One request wins (smaller id); the other side has already said yes by pressing Record, and tells anyone who agreed to its own request that it's off |
| Leaving the lesson or disconnecting | Recording stops here, and on the others' computers when they see you leave; the summary is saved |
| Quitting the app while recording | A Stop is sent, and the summary is saved with the length so far |

## How it works
- Control messages travel on the lesson's encrypted peer chat channel (`sendChatEvent`) with the tag `canduet-recording-1`. The processor keeps them out of the chat history; the editor queues them (max 64) and handles them on the message thread.
- Message JSON: `{"v":1,"t":"request|answer|start|mark|stop|alive","id":"<16 chars>", "ok":bool, "label":"…", "ago":seconds, "people":[…]}`.
- While recording, each computer sends `alive` every 10 s. Anyone not recording that id answers `stop` (covers a lost Stop). An `alive` reaching someone who agreed but missed the Start starts their copy. Silence for 30 s from anyone in the recording stops it. A one-second timer in the editor drives this (`RecordingConsent::tick`). Anything malformed, out of range or for another request is ignored. Labels are one line, at most 80 characters.
- A highlight's time is taken on each computer from its own recording clock: the sender stamps it when Mark is pressed; the receiver uses arrival time minus `ago` (time spent naming it). Network delay (tens of ms) is the only difference between copies.
- Summary file `<recording name>.canduet.json` beside the recording: `{"v":1,"title","started","duration","people","audio","highlights":[{"t","label","by"}]}`. `audio` must be a plain file name in the same folder. Summaries are found in the recording folder and its subfolders.
- Code: `Source/TinCanRecording.{h,cpp}` (messages, consent rules, summaries; no UI), `Source/TinCanSummaries.{h,cpp}` (Summaries tab), editor glue in `SonobusPluginEditor.cpp` (`onRecordPressed`, `applyRecording`, `markPressed`, `saveLessonSummary`). Each step is written to `%APPDATA%\CanDuet\connection-log.txt` as `recording[<run>] …` (no names, labels or paths).

## Limits (known)
- The sender's name in a message is the name it gives itself. That's acceptable inside an encrypted lesson room, where only ticket holders can be.
- Recordings and summaries stay on each computer. Uploading them to the student/teacher accounts is future website work (site `docs/APP_TOOLS.md`).
- With multi-track recording turned on in Options, the summary points at the main file SonoBus reports; playback from Summaries is only tested with the default mix file.
- Naming a highlight in the box wasn't checked through the real window (UI Automation can't type into JUCE text boxes); unit tests cover names travelling between copies.

## QA review 2026-11-05 and fixes
The first review found that someone who left and came back under the same name could be recorded again without being asked, and that a lost Stop went unnoticed. Fixed as above (leave stops it; `alive` checks; timeouts for stale questions; yield sends Stop; the dialog uses a counter instead of comparing window addresses). Also fixed: deleting the recording open in the player. Still open (minor): a stranger joining in the instant between agreeing and Start; very long names can make Start too big for one packet; the Summaries scan runs on the message thread.

## Tests
- `TinCanAudioTests` (`cmake --build build --config Release --target TinCanAudioTests`): messages round-trip and rejection of malformed ones, one-to-one flow, decline, cancel, alone, stranger joins, leave before answering, both press Record, group of three; after the review also leave → stop and no silent re-entry, lost Stop recovered by `alive`, silence → stop, lost Start recovered, stale question times out and doesn't block the next, wrong-id `alive` gets a Stop; summary save/load/sort, path escape rejected, time format. All pass.
- After the fixes (two copies on one PC): normal cycles still pass; quitting one copy while recording stopped the other within 2 s; Delete on the recording open in the player closed the player and removed both files.
- Two copies on one PC (local `CanDuetServer` + `scripts/tincan-mock-redeem.py`, driven by UI Automation), 2026-11-05:
  - Question shown only on the other copy; nothing recorded before the answer.
  - "Not now" → nobody records. Ask again → Allow → both record.
  - 4 full cycles, alternating who asks and who stops: both copies record (~1.3 MB each for 12 s), stopping on one stops both, both summaries list both people and the same two highlights at the same times (4.2 s and 8.3 s).
  - Summaries tab: highlight → Play highlight loads the file in the main player and plays; Stop stops.
  - In the first session, the second copy's audio device wasn't running (86-byte file, length 0). A restart fixed it; this is a two-copies-on-one-PC artefact, not seen in the later runs.
