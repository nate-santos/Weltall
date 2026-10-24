# Rebuild the APK/browser PNG fallbacks from the same artwork and placement as the adaptive icon.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$resourceRoot = Join-Path $PSScriptRoot '../Source/Weltall/Android/res'
[xml]$foreground = Get-Content -Raw (Join-Path $resourceRoot 'drawable/weltall_icon_foreground.xml')
$android = 'http://schemas.android.com/apk/res/android'
$left = [double]($foreground.inset.GetAttribute('insetLeft', $android) -replace 'dp$', '')
$top = [double]($foreground.inset.GetAttribute('insetTop', $android) -replace 'dp$', '')
$right = [double]($foreground.inset.GetAttribute('insetRight', $android) -replace 'dp$', '')
$bottom = [double]($foreground.inset.GetAttribute('insetBottom', $android) -replace 'dp$', '')
[xml]$background = Get-Content -Raw (Join-Path $resourceRoot 'drawable/weltall_icon_background.xml')
$color = [System.Drawing.ColorTranslator]::FromHtml($background.shape.solid.GetAttribute('color', $android))
$art = [System.Drawing.Image]::FromFile((Join-Path $resourceRoot 'drawable-nodpi/weltall_icon_art.png'))
$sizes = @{ 'drawable'=512; 'drawable-ldpi'=36; 'drawable-mdpi'=48; 'drawable-hdpi'=72;
    'drawable-xhdpi'=96; 'drawable-xxhdpi'=144; 'drawable-xxxhdpi'=192 }
try {
    foreach ($entry in $sizes.GetEnumerator()) {
        $size = [int]$entry.Value
        # Supersample the circle and artwork before making the density-specific PNG.
        $renderSize = $size * 4
        $scale = $renderSize / 72.0
        $canvas = New-Object System.Drawing.Bitmap($renderSize, $renderSize)
        $graphics = [System.Drawing.Graphics]::FromImage($canvas)
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $brush = New-Object System.Drawing.SolidBrush($color)
        $graphics.FillEllipse($brush, 0, 0, $renderSize, $renderSize)
        $clip = New-Object System.Drawing.Drawing2D.GraphicsPath
        $clip.AddEllipse(0, 0, $renderSize, $renderSize)
        $graphics.SetClip($clip)
        # Adaptive layers are 108dp; the visible viewport starts 18dp from each edge.
        $rect = [System.Drawing.RectangleF]::new(($left - 18) * $scale, ($top - 18) * $scale,
            (108 - $left - $right) * $scale, (108 - $top - $bottom) * $scale)
        $graphics.DrawImage($art, $rect)
        $output = New-Object System.Drawing.Bitmap($size, $size)
        $resize = [System.Drawing.Graphics]::FromImage($output)
        $resize.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $resize.DrawImage($canvas, 0, 0, $size, $size)
        $folder = Join-Path $resourceRoot $entry.Key
        [System.IO.Directory]::CreateDirectory($folder) | Out-Null
        $output.Save((Join-Path $folder 'icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        $resize.Dispose()
        $output.Dispose()
        $graphics.Dispose()
        $clip.Dispose()
        $brush.Dispose()
        $canvas.Dispose()
    }
} finally {
    $art.Dispose()
}
