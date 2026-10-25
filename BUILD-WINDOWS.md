# Building CanDuet (Tin Can Studios fork of SonoBus) on Windows

Status: **verified 2026-10-22** on Windows 11 with VS 2022 (v143) and CMake 4.0.2 — see the build log at the bottom. Tin Can client changes are described in `doc/TINCAN-CLIENT.md`.

## 1. Install the tools (one time)
1. **Git for Windows** — https://git-scm.com/download/win (also gives you Git Bash).
2. **CMake 3.15 or newer** — https://cmake.org/download/ (tick "Add CMake to the system PATH").
3. **Visual Studio 2022 Community** — https://visualstudio.microsoft.com/
   - Workload: **Desktop development with C++**.
   - The default **v143** toolset and a **Windows 10/11 SDK** are enough. The build uses the JUCE **7.0.8** copy in `deps/juce` (not the older `JUCE/` folder), which supports VS 2022; the v142 toolset is not needed.
4. **Steinberg ASIO SDK** — https://www.steinberg.net/developers/ (free; GPLv3 licence option since Oct 2025).
5. **Inno Setup 6** (only for making the installer) — https://jrsoftware.org/isinfo.php

## 2. Folder layout
The build looks for the ASIO SDK **next to** the repo, at `..\asiosdk\common`:
```
C:\dev\
├── tincansonobus\      ← this repo
└── asiosdk\            ← rename the extracted folder (e.g. asiosdk_2.3.3_...) to exactly "asiosdk"
    └── common\
```
```powershell
cd C:\dev
git clone https://github.com/ale-alle/tincansonobus.git
```
JUCE, AOO and prebuilt Opus libraries (`deps\windows\Release`) are already inside the repo — nothing else to download.

## 3. Configure and build (Developer PowerShell for VS 2022)
```powershell
cd C:\dev\tincansonobus
cmake -G "Visual Studio 17 2022" -A x64 -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --config Release --parallel --target SonoBus_Standalone
```
Output: `build\SonoBus_artefacts\Release\Standalone\CanDuet.exe`. Drop `--target …` to also build the VST3 plugins (not needed for lessons).

`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` is required with CMake 4.x (bundled dependencies declare very old minimum versions) and harmless on 3.x.

### Tin Can build options
| CMake option | Default | Purpose |
|---|---|---|
| `TINCAN_API_BASE` | `https://tincanstudios.ca` | App server that redeems `canduet://join` tickets |
| `TINCAN_SERVER_HOST` / `TINCAN_SERVER_PORT` | empty → `aoo.sonobus.net:10998` | Default connection server; set to ours once T-023 exists |

Example: `cmake -B build -DTINCAN_SERVER_HOST=connect.tincanstudios.ca`.

### Unit tests
```powershell
cmake --build build --config Release --target TinCanTests
build\Release\TinCanTests.exe      # prints "all checks passed"
```

Debug build (for stepping through code in Visual Studio): `cmake --build build --config Debug`, or open `build\SonoBus.sln`.

The original scripts (`setupcmakewin.sh`, `buildcmake.sh`) do the same from Git Bash but target the VS 2019 generator.

## 3b. Test encryption and join links on one PC
The app is single-instance (so a clicked `canduet://` link reaches the running copy); `--multi-instance` allows two copies. The first copy hosts a connection server on port 10999. Allowing the Windows Firewall prompt is not needed for this local test.

**Encryption** — same key connects, different keys must not:
```powershell
$exe = ".\build\SonoBus_artefacts\Release\Standalone\CanDuet.exe"
$k = [Convert]::ToBase64String((1..32 | ForEach-Object { Get-Random -Maximum 256 }))
& $exe --multi-instance -c localhost:10999 -g enctest -n A -k $k
& $exe --multi-instance -c localhost:10999 -g enctest -n B -k $k
```
Both show 2 users within seconds. Repeat with a second, different key for B: each stays at 1 user.

**Join link** — with the mock redeem API standing in for T-019:
```powershell
python scripts\tincan-mock-redeem.py           # terminal 1
$env:TINCAN_API_BASE = "http://localhost:8788" # terminal 2
& $exe --multi-instance "canduet://join?ticket=demo-ok"   # run twice
```
Both join "Demo lesson" and connect; room name, password and share-link button are hidden. `ticket=wrong` shows "Can't join lesson".

**Video** (T-009, [doc/TINCAN-VIDEO.md](doc/TINCAN-VIDEO.md)) — needs a local LiveKit server (`livekit-server.exe` from github.com/livekit/livekit/releases) and the video page:
```powershell
livekit-server.exe --dev --bind 127.0.0.1                          # terminal 1 (key devkey / secret secret)
python -m http.server 8790 --bind 127.0.0.1 --directory video-page # terminal 2
$env:MOCK_VIDEO_URL = "ws://localhost:7880"; python scripts\tincan-mock-redeem.py   # terminal 3
$env:TINCAN_API_BASE = "http://localhost:8788"                     # terminal 4
$env:TINCAN_VIDEO_PAGE = "http://localhost:8790/"
& $exe --multi-instance "canduet://join?ticket=demo-ok"            # run twice
```
Each copy opens a "Demo lesson - Video" window; allow the camera when asked. With one webcam, the second copy usually can't open it and says so; its window still shows the first copy's video. Closing the video window keeps the audio going; leaving the lesson closes it.

## 4. Smoke test
1. Run `CanDuet.exe`; pick your audio interface with the **ASIO** driver type; buffer 128 or 256 samples.
2. Run a second copy on another PC (or a phone with SonoBus) and join the same private group with a password via the default server.
3. Note: this still uses `aoo.sonobus.net` and sends unencrypted peer-to-peer audio — test only with people who know that (D-009 in the tincanstudios-site docs).

## 5. Installer (optional for now)
`release\wininstaller.iss` builds the installer with Inno Setup. It expects the files staged by `release\distwin.sh` and a code-signing setup (`SignTool=`); for unsigned test builds, comment out the `SignTool`/`SignedUninstaller` lines. It also registers the `canduet://` link handler (T-018). Not yet built or tested with Inno Setup.

## Troubleshooting
| Symptom | Likely fix |
|---|---|
| `asio.h` / `asiosys.h` not found | ASIO SDK not at `..\asiosdk\common` (check folder name) |
| `opus.lib` not found | Building Win32 without `deps\windows\Release32`, or wrong config — use `-A x64` and `Release`/`Debug` |
| `Compatibility with CMake < 3.5 has been removed` | Add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (CMake 4.x) |
| No ASIO devices listed | Install your interface's ASIO driver (or ASIO4ALL for built-in audio) |

## Build log
| Date | Windows | VS / toolset | Result | Notes |
|---|---|---|---|---|
| 2026-10-22 | Windows 11 Home 26200 | VS 2022 Community, MSVC 14.44 (v143), CMake 4.0.2, ASIO SDK 2.3.3 | ✅ Release x64 | Unmodified fork, then with Tin Can changes. `--version` → 1.7.2. Needed the CMake policy flag. ~540 warnings, all upstream. TinCanTests pass; §3b tests pass (same key connects, different keys don't, join link OK, bad ticket → error) |
| 2026-10-25 | Windows 11 Home 26200 | same | ✅ Release x64 | Branch `t-009-video`: WebView2 SDK 1.0.2903.40 fetched by CMake, `WebView2Loader.dll` copied next to the exe. Video test (§3b) with fake cameras: both copies open the video window and receive each other's video; token removed from the page address |
