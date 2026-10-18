# Building Tin Can (SonoBus fork) on Windows

Status: written from the build files in this repo (`CMakeLists.txt`, `setupcmakewin.sh`, `buildcmake.sh`, `release/`). **Not yet tested on a Windows machine** — record anything that differs in the "Build log" at the bottom.

## 1. Install the tools (one time)
1. **Git for Windows** — https://git-scm.com/download/win (also gives you Git Bash).
2. **CMake 3.15 or newer** — https://cmake.org/download/ (tick "Add CMake to the system PATH").
3. **Visual Studio 2022 Community** — https://visualstudio.microsoft.com/
   - Workload: **Desktop development with C++**.
   - Individual components: **MSVC v142 – VS 2019 C++ x64/x86 build tools** and a **Windows 10/11 SDK**.
   - Why v142: the bundled JUCE (6.0.8, 2021) predates VS 2022; building with the VS 2019 toolset avoids compiler surprises. Try the default v143 toolset later if you like.
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
cmake -G "Visual Studio 17 2022" -A x64 -T v142 -B build
cmake --build build --config Release --parallel
```
Output: `build\SonoBus_artefacts\Release\Standalone\SonoBus.exe` (plus a VST3 plugin, which we don't need for lessons).

Debug build (for stepping through code in Visual Studio): `cmake --build build --config Debug`, or open `build\SonoBus.sln`.

The original scripts (`setupcmakewin.sh`, `buildcmake.sh`) do the same from Git Bash but target the VS 2019 generator.

## 4. Smoke test
1. Run `SonoBus.exe`; pick your audio interface with the **ASIO** driver type; buffer 128 or 256 samples.
2. Run a second copy on another PC (or a phone with SonoBus) and join the same private group with a password via the default server.
3. Note: this still uses `aoo.sonobus.net` and sends unencrypted peer-to-peer audio — test only with people who know that (D-009 in the tincanstudios-site docs).

## 5. Installer (optional for now)
`release\wininstaller.iss` builds the installer with Inno Setup. It expects the files staged by `release\distwin.sh` and a code-signing setup (`SignTool=`); for unsigned test builds, comment out the `SignTool`/`SignedUninstaller` lines. This is also where the `tincan://` link handler will be registered later (T-018).

## Troubleshooting
| Symptom | Likely fix |
|---|---|
| `asio.h` / `asiosys.h` not found | ASIO SDK not at `..\asiosdk\common` (check folder name) |
| `opus.lib` not found | Building Win32 without `deps\windows\Release32`, or wrong config — use `-A x64` and `Release`/`Debug` |
| Generator/toolset error | Install the **MSVC v142** component, or drop `-T v142` to try the default toolset |
| No ASIO devices listed | Install your interface's ASIO driver (or ASIO4ALL for built-in audio) |

## Build log
| Date | Windows | VS / toolset | Result | Notes |
|---|---|---|---|---|
| | | | | |
