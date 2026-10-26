# Lesson workspace (T-037)

A board for lesson material inside CanDuet: **Tools → Workspace → Open the workspace**. Spec: `.claude/worktrees/asiosdk/CanDuet_Features.md` §4; task T-037 and decision D-019 in the site repo.

## What works (stage 1, local)
- Cards on a board: **images** (PNG, JPEG, GIF, WebP, up to 100 MB; *Open file…* or drop on the board) and blank **whiteboards**. Drag the title bar to move, the corner to resize (keeps the shape), **–** to hide, **✕** to close. The *Materials* list shows every card with a show/hide box; *List* hides the list.
- Tools: **Move**, **Pen**, **Highlight** (wide, see-through; yellow by default), **Arrow**, **Text** (click, type, Enter; Escape cancels), **Eraser** (click a mark), 7 colours, **Undo** (Ctrl+Z, your own marks), **Clear card**, **Clear all**, **Reset** (teacher). Keys M P H A T E pick tools.
- Marks are stored relative to their card, so they stay put when a card or the window is resized.
- Closing the window hides it; the board is still there when it's opened again (until CanDuet quits).
- Files never leave the computer: they're opened as local blob URLs.

**Not yet:** PDFs (see *Decisions needed*), sharing with the other person (stage 2), keeping the board after CanDuet quits.

## How it's built
- The page is plain HTML/JS in `workspace-page/` (`index.html`, `workspace-model.js`, `workspace-ui.js`), shown in WebView2 (D-019) by `Source/TinCanWorkspace.{h,cpp}`. The three files are compiled into the app (BinaryData) and written to `%APPDATA%\CanDuet\workspace\` when the window opens, so the page always matches the build and needs no internet. `TINCAN_WORKSPACE_PAGE=<folder>` loads the page from a folder instead (edit and reopen, no rebuild).
- The web view only allows the page itself (links and new windows don't open). Nothing touches the audio engine.
- **Model** (`workspace-model.js`, no DOM): every change is an operation `{v:1, type, id, by, ts, role, …}` — `addCard`, `updateCard` (move, resize, page, hide, rename), `removeCard`, `addMark`, `removeMark`, `clearMarks`, `reset`. The page applies its own changes through the same `applyOp` the other person's will use.
  - **Validation:** `validateOp` checks version, type, exact field set, id format, sizes (40 cards, 4000 marks, 2000 points a stroke, 500-character text, 120-character names), colours from a fixed palette and coordinates in range. Anything else is dropped.
  - **Ordering:** Lamport timestamps, ties broken by author. Card fields are last-writer-wins per field; marks are drawn in (time, author) order. Both sides end up the same whatever order messages arrive in.
  - **Ownership:** the teacher can do anything. A student adds cards and marks and changes their own, but can't remove other people's things, clear the teacher's marks or reset. Removals leave tombstones so a late "add" can't bring something back.
  - **Join / reconnect:** `snapshot()` is the whole state as operations; `mergeSnapshot(state, snap, senderRole)` folds it in with the same rules. The sender's role comes from what the app knows (who the lesson's teacher is), never from the message, and only the teacher's snapshot can remove things the receiver still has.

## Tests
- `node --test workspace-page/tests/workspace-model.test.js` (Node 18+, no packages): 9 tests — ordering out of sequence, validation of hostile input, ownership, last-writer-wins on both sides, tombstones, clear vs. later marks, reset, snapshot join/rejoin, forged snapshots, flood limits.
- Checked in headless Edge (same engine as WebView2) through the DevTools protocol: opening an image through the file input, adding a whiteboard, pen/arrow/highlight strokes with the mouse, moving a card; no script errors.
- Checked in the app: the Workspace tab opens the window, the page loads from the app's own files, *+ Whiteboard* works (driven through UI Automation), closing hides and reopening keeps the board, CanDuet exits cleanly.
- **Manual (needs a person):** drawing with a real mouse or pen tablet, text tool, eraser, resizing by hand, high-DPI look.

## Decisions needed (owner)
1. **PDFs.** D-019 chose WebView2's built-in PDF viewer. In practice it can't be pinned to a page: it fits the page only when it first loads, keeps its own zoom and scroll after a resize, and replacing it doesn't reliably re-fit (tested at three card widths: marks drawn at the page's corners landed off the page). So annotations would drift. Options:
   - **pdf.js** (Mozilla, Apache-2.0, GPL-compatible) bundled with the page: renders each page to a canvas we control, exact geometry, works offline, still inside WebView2. *Recommended.* New dependency (about 1.5 MB of JS in the app).
   - Windows.Data.Pdf (built into Windows): render pages to images in C++, hand them to the page. No new library, more C++ and a JS bridge.
   - Keep the built-in viewer for viewing only, no drawing on PDFs.
2. **How the board reaches the other person (stage 2).** The model is ready; only the transport is open.
   - **LiveKit data channel, workspace inside the video page** (`video-page/`). Reliable, ordered, encrypted (DTLS); reconnects and join events built in; can send files (images, PDFs) reliably. Needs video (our LiveKit server) to be running for the lesson. *Recommended.*
   - **SonoBus peer messages** (the chat channel, as T-037 first said). Already encrypted with the lesson key and needs no video, but it's plain UDP with a 4 KB packet limit: we'd have to add acknowledgements, resends and chunked file transfer, and JUCE 7.0.8's web view has no JavaScript bridge, so messages between the page and C++ would go through navigation tricks.
