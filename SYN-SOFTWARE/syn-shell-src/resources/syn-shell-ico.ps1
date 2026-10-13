<#
  Makes syn-shell.ico (the Windows exe's and window's icon) from
  syn-shell.svg: rsvg-convert draws the SVG at each size Windows asks
  icons for, and those PNGs go into one .ico. Run it again whenever the
  SVG changes.

    .\syn-shell-ico.ps1 [-Msys C:\msys64]

  Needs, in MSYS2:  pacman -S mingw-w64-ucrt-x86_64-librsvg
#>
param(
  [string]$Msys = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } elseif (Test-Path "$env:USERPROFILE\msys64") { "$env:USERPROFILE\msys64" } else { 'C:\msys64' })
)
$ErrorActionPreference = 'Stop'
$rsvg = Join-Path $Msys 'ucrt64\bin\rsvg-convert.exe'
if (-not (Test-Path $rsvg)) { throw "No rsvg-convert under $Msys (see the top of this script)." }
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$svg = Join-Path $PSScriptRoot 'syn-shell.svg'
$pngs = foreach ($n in $sizes) {
  $png = [IO.Path]::GetTempFileName()
  & $rsvg -w $n -h $n -o $png $svg
  if ($LASTEXITCODE) { throw "rsvg-convert failed at $n px" }
  , [IO.File]::ReadAllBytes($png)
  Remove-Item $png
}
# ICONDIR, one ICONDIRENTRY per size (0 means 256), then the PNGs.
$out = New-Object IO.MemoryStream
$w = New-Object IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $n = $sizes[$i] % 256
  $w.Write([byte]$n); $w.Write([byte]$n); $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1); $w.Write([uint16]32)
  $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
  $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Flush()
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'syn-shell.ico'), $out.ToArray())
"wrote syn-shell.ico ($($sizes -join ', ') px)"
