# Renders the CanDuet app icon: the single Tin Can Studios can (images\canduet_can.svg,
# the same artwork as the tincanstudios.ca favicon), centred on a transparent square.
# Uses headless Microsoft Edge to rasterise the SVG. Run from the repo root:
#   powershell -ExecutionPolicy Bypass -File scripts\make-canduet-icon.ps1
# Writes images\canduet_icon_1024.png and canduet_icon_256.png (app and installer icon)
# and images\canduet_logo_96.png (the logo inside the app)

$root = Split-Path -Parent $PSScriptRoot
$svg = Join-Path $root "images\canduet_can.svg"
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
          "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw "Microsoft Edge not found" }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("canduet-icon-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $svgUrl = ([System.Uri]$svg).AbsoluteUri
    foreach ($size in 1024, 256, 96) {
        # the can is 17:24, so it fills the height with a small margin
        $pad = [int]($size * 0.04)
        $html = Join-Path $tmp "icon$size.html"
        Set-Content -Encoding utf8 -Path $html -Value @"
<!doctype html><html><body style="margin:0;background:transparent;width:${size}px;height:${size}px;display:flex;align-items:center;justify-content:center">
<img src="$svgUrl" style="height:$($size - 2 * $pad)px">
</body></html>
"@
        $name = if ($size -eq 96) { "canduet_logo_96.png" } else { "canduet_icon_$size.png" }
        $out = Join-Path $root "images\$name"
        $profile = Join-Path $tmp "profile$size"
        & $edge --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 `
            --user-data-dir="$profile" --default-background-color=00000000 `
            --window-size="$size,$size" --screenshot="$out" ([System.Uri]$html).AbsoluteUri 2>&1 | Out-Null
        if (-not (Test-Path $out)) { throw "Edge did not write $out" }
    }
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
"Wrote images\canduet_icon_1024.png, canduet_icon_256.png and canduet_logo_96.png"
