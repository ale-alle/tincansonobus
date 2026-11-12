# Builds the CanDuet Windows installer (T-025) from a Release build in ..\build.
#   release\build-installer.ps1                 unsigned (pilot)
#   release\build-installer.ps1 -Sign           signed: needs signtool (Windows SDK) and
#                                               $env:CANDUET_CERT_FILE / $env:CANDUET_CERT_PASSWORD
# Needs Inno Setup 6.4+ (ISCC.exe on PATH, or installed in the usual place).
# The version comes from project(... VERSION x.y.z) in CMakeLists.txt; the script refuses an exe built
# from another version, so the installer, the app and the site's /app/version.json always agree.
# Output: release\instoutput\CanDuet-<version>-Setup.exe and its SHA-256 (for the Download page).
#   release\build-installer.ps1 -Staging      from build-staging\ (TINCAN_API_BASE=https://staging.tincanstudios.ca,
#                                             docs/OPERATIONS.md "Staging" in the site repo) into instoutput-staging\, under
#                                             the live file name so the staging Download page and update check find it.
#                                             Same app name and canduet:// links: a PC has either the staging or the live app.
param([switch]$Sign, [switch]$Staging)
$ErrorActionPreference = "Stop"

$release = $PSScriptRoot
$repo = Split-Path -Parent $release
$built = Join-Path $repo ($(if ($Staging) { "build-staging" } else { "build" }) + "\SonoBus_artefacts\Release\Standalone")

$cmake = Get-Content (Join-Path $repo "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(SonoBus VERSION (\d+\.\d+\.\d+)\)') { throw "no project(SonoBus VERSION x.y.z) in CMakeLists.txt" }
$version = $Matches[1]

$exe = Join-Path $built "CanDuet.exe"
if (-not (Test-Path $exe)) { throw "no Release build at $exe (cmake --build $(if ($Staging) { 'build-staging' } else { 'build' }) --config Release --target SonoBus_Standalone)" }
$exeVersion = (Get-Item $exe).VersionInfo.ProductVersion
if ($exeVersion -ne $version) {
    throw "CanDuet.exe is version '$exeVersion' but CMakeLists.txt says ${version}: rebuild first"
}

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = foreach ($v in "6", "7") {
        foreach ($root in "${env:ProgramFiles(x86)}", "$env:ProgramFiles", "$env:LOCALAPPDATA\Programs") {
            $candidate = Join-Path $root "Inno Setup $v\ISCC.exe"
            if (Test-Path $candidate) { $candidate }
        }
    }
    $iscc = $iscc | Select-Object -First 1
}
if (-not $iscc) { throw "Inno Setup not found (https://jrsoftware.org/isdl.php)" }

$stage = Join-Path $release "CanDuet"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item $exe, (Join-Path $built "WebView2Loader.dll") $stage
# Practice mode's own copy of the lesson page (T-075), straight from video-page\ so an edit to the
# page alone (no relink) or a removed font can't ship stale
$lesson = New-Item -ItemType Directory (Join-Path $stage "lesson")
$page = Join-Path $repo "video-page"
Copy-Item (Join-Path $page "lesson.html") $lesson
Copy-Item (Join-Path $page "fonts"), (Join-Path $page "cans") $lesson -Recurse

$out = Join-Path $release $(if ($Staging) { "instoutput-staging" } else { "instoutput" })
New-Item -ItemType Directory -Force $out | Out-Null
$setup = Join-Path $out "CanDuet-$version-Setup.exe"
if (Test-Path $setup) { Remove-Item $setup -Confirm:$false }

$isccArgs = @("/Q", "/O$out", "/DSBVERSION=$version")
if ($Sign) {
    if (-not $env:CANDUET_CERT_FILE) { throw "-Sign needs `$env:CANDUET_CERT_FILE (and CANDUET_CERT_PASSWORD)" }
    $signtool = "signtool.exe sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 " +
                "/f `$q$env:CANDUET_CERT_FILE`$q /p `$q$env:CANDUET_CERT_PASSWORD`$q `$f"
    $isccArgs += @("/DSIGN", "/Ssigntool=$signtool")
}
& $iscc @isccArgs (Join-Path $release "wininstaller.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
Remove-Item $stage -Recurse -Force -Confirm:$false

$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
$size = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Output "$setup"
Write-Output "version $version, $size MB, $(if ($Sign) { 'signed' } else { 'UNSIGNED' })"
Write-Output "sha256 $hash"
