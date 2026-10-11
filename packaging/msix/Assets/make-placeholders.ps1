<#
.SYNOPSIS
    Draws the placeholder logos the MSIX manifest names.

.DESCRIPTION
    Until the application has a designed icon: a monogram in the sizes the
    manifest needs. Replace the PNGs with the real artwork under the same
    names and sizes; this script is only for regenerating the placeholders.
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = @{
    'StoreLogo.png'         = 50
    'Square44x44Logo.png'   = 44
    'Square150x150Logo.png' = 150
}
foreach ($name in $sizes.Keys) {
    $n = $sizes[$name]
    $bmp = New-Object System.Drawing.Bitmap $n, $n
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 31, 78, 121))
    $g.FillEllipse($brush, 1, 1, $n - 2, $n - 2)
    $font = New-Object System.Drawing.Font 'Segoe UI', ([single]($n * 0.42)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = 'Center'
    $format.LineAlignment = 'Center'
    $rect = New-Object System.Drawing.RectangleF 0, 0, $n, $n
    $g.DrawString('BT', $font, [System.Drawing.Brushes]::White, $rect, $format)
    $g.Dispose()
    $bmp.Save((Join-Path $PSScriptRoot $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
