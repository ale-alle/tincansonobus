# Makes the folder to upload to the test server for server/vps/install-relay.sh.
#   scripts\make-relay-upload.ps1 -OutDir <folder outside the repo>
param([Parameter(Mandatory)][string]$OutDir)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path $OutDir "canduet-relay-setup"

if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
foreach ($dir in "server\relay", "deps\aoo\lib\aoo", "deps\aoo\lib\src", "deps\aoo\deps\md5", "deps\aoo\deps\oscpack\osc") {
    $target = Join-Path $out $dir
    New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item (Join-Path $repo "$dir\*") $target -Recurse
}
Copy-Item (Join-Path $repo "server\vps\install-relay.sh") $out

# The server is Linux: no Windows line endings in the install script
$script = Join-Path $out "install-relay.sh"
$text = [IO.File]::ReadAllText($script) -replace "`r`n", "`n"
[IO.File]::WriteAllText($script, $text, (New-Object Text.UTF8Encoding $false))
Write-Output $out
