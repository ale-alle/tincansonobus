# Draws the placeholder CanDuet app icon: two tin cans joined by a taut string.
# Placeholder until a designed icon exists (design-ux). Run from the repo root:
#   powershell -ExecutionPolicy Bypass -File scripts\make-canduet-icon.ps1
# Writes images\canduet_icon_1024.png and images\canduet_icon_256.png

Add-Type -AssemblyName System.Drawing

function Draw-Can($g, [float]$cx, [float]$cy, [float]$w, [float]$h, [float]$angle, $body, $rim, $band) {
    $state = $g.Save()
    $g.TranslateTransform($cx, $cy)
    $g.RotateTransform($angle)
    $x = -$w / 2; $y = -$h / 2; $e = $w * 0.32
    # body
    $g.FillRectangle($body, $x, $y, $w, $h)
    $g.FillEllipse($body, $x, $y + $h - $e / 2, $w, $e)
    # ribs
    $pen = New-Object System.Drawing.Pen($band, [float]($w * 0.035))
    foreach ($f in 0.3, 0.5, 0.7) {
        $g.DrawArc($pen, $x, $y + $h * $f - $e / 2, $w, $e, 0, 180)
    }
    # open top (rim + dark inside)
    $g.FillEllipse($rim, $x, $y - $e / 2, $w, $e)
    $inner = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 70, 78, 92))
    $g.FillEllipse($inner, $x + $w * 0.08, $y - $e / 2 + $e * 0.12, $w * 0.84, $e * 0.76)
    $g.Restore($state)
}

function Make-Icon([int]$size, [string]$path) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = $size / 1024.0
    $g.ScaleTransform($s, $s)

    # rounded tile background
    $r = 200
    $tile = New-Object System.Drawing.Drawing2D.GraphicsPath
    $tile.AddArc(40, 40, $r, $r, 180, 90); $tile.AddArc(984 - $r, 40, $r, $r, 270, 90)
    $tile.AddArc(984 - $r, 984 - $r, $r, $r, 0, 90); $tile.AddArc(40, 984 - $r, $r, $r, 90, 90)
    $tile.CloseFigure()
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point(0, 0)), (New-Object System.Drawing.Point(1024, 1024)),
        [System.Drawing.Color]::FromArgb(255, 34, 52, 92), [System.Drawing.Color]::FromArgb(255, 18, 26, 48))
    $g.FillPath($bg, $tile)

    # the taut string (drawn first so the cans sit on top of its ends)
    $string = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 170, 64), 22)
    $string.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $string.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($string, 330, 560, 694, 560)

    $body = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 196, 204, 216))
    $rim  = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 236, 240, 246))
    $band = [System.Drawing.Color]::FromArgb(255, 150, 160, 176)
    # cans lie on their sides, open ends out, string through the bottoms (like the real toy)
    Draw-Can $g 262 560 180 280 -90 $body $rim $band
    Draw-Can $g 762 560 180 280 90 $body $rim $band

    # small music note above the string
    $note = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 170, 64))
    $g.FillEllipse($note, 452, 352, 70, 54)
    $g.FillRectangle($note, 508, 230, 16, 150)
    $g.FillPolygon($note, [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF(524, 230)), (New-Object System.Drawing.PointF(590, 262)),
        (New-Object System.Drawing.PointF(590, 300)), (New-Object System.Drawing.PointF(524, 270))))

    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}

$root = Split-Path -Parent $PSScriptRoot
Make-Icon 1024 (Join-Path $root "images\canduet_icon_1024.png")
Make-Icon 256  (Join-Path $root "images\canduet_icon_256.png")
"Wrote images\canduet_icon_1024.png and images\canduet_icon_256.png"
