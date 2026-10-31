# Builds a zip a tester can run with no development tools (doc/TINCAN-VPS.md, section 2).
#   scripts\make-tester-kit.ps1 -Ticket <ticket> -Role Student -OutDir <folder outside the repo>
# The zip holds a join ticket: share it privately and keep it out of git.
param(
    [Parameter(Mandatory)][string]$Ticket,
    [Parameter(Mandatory)][string]$Role,
    [Parameter(Mandatory)][string]$OutDir,
    [string]$ApiBase = "https://rtc.tincanstudios.ca:8443"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo "build\SonoBus_artefacts\Release\Standalone"
$kit = Join-Path $OutDir "CanDuet-Test-$Role"

if (Test-Path $kit) { Remove-Item $kit -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force $kit | Out-Null
Copy-Item (Join-Path $build "CanDuet.exe"), (Join-Path $build "WebView2Loader.dll") $kit
Copy-Item (Join-Path $repo "doc\TESTER-README.txt") (Join-Path $kit "READ ME FIRST.txt")

$bat = "@echo off`r`nset TINCAN_API_BASE=$ApiBase`r`nstart `"`" `"%~dp0CanDuet.exe`" `"canduet://join?ticket=$Ticket`"`r`n"
[IO.File]::WriteAllText((Join-Path $kit "Join test lesson.bat"), $bat, [Text.Encoding]::ASCII)

$zip = "$kit.zip"
Compress-Archive -Path $kit -DestinationPath $zip -Force
Remove-Item $kit -Recurse -Force -Confirm:$false
Write-Output $zip
