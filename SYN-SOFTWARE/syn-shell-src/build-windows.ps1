<#
  SYN-SHELL for Windows: builds it with MSYS2's UCRT64 toolchain and
  gathers everything it needs to run into one folder (default dist\):
  syn-shell.exe, syn-shell-attach.exe, the Qt libraries and plugins
  (windeployqt) and every other DLL they load from MSYS2 (libarchive and
  its compressors, libvterm, KSyntaxHighlighting, the C++ runtime),
  found by following each one's imports.

    .\build-windows.ps1 [-Msys C:\msys64] [-Out dist] [-Zip]

  Needs, in MSYS2:  pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,
                    qt6-base,libarchive,libvterm,syntax-highlighting}
  Optional at run time, on PATH: git (status marks), poppler's pdftoppm
  and pdfinfo (PDF previews), ffmpeg and ffprobe (video and audio).
#>
param(
  [string]$Msys = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } elseif (Test-Path "$env:USERPROFILE\msys64") { "$env:USERPROFILE\msys64" } else { 'C:\msys64' }),
  [string]$Out = (Join-Path $PSScriptRoot 'dist'),
  [string]$Build = (Join-Path $PSScriptRoot 'build-windows'),
  [switch]$Zip
)
$ErrorActionPreference = 'Stop'
# Windows won't replace a program that's running.
$outFull = [IO.Path]::GetFullPath($Out).TrimEnd('\') + '\'
$running = Get-Process -Name 'syn-shell*' -ErrorAction SilentlyContinue |
  Where-Object { $_.Path -and $_.Path.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase) }
if ($running) { throw "SYN-SHELL is running from ${Out}: close it (and any shell it moved to Windows Terminal), then run this again." }

$ucrt = Join-Path $Msys 'ucrt64'
$bin = Join-Path $ucrt 'bin'
if (-not (Test-Path (Join-Path $bin 'cmake.exe'))) { throw "No UCRT64 toolchain under $Msys (see the top of this script)." }
$env:PATH = "$bin;$env:PATH"

cmake -S $PSScriptRoot -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'cmake configure failed' }
cmake --build $Build
if ($LASTEXITCODE) { throw 'build failed' }

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory $Out | Out-Null
cmake --install $Build --prefix $Out
if ($LASTEXITCODE) { throw 'install failed' }

# Qt's own: its DLLs, the platform and style plugins, image formats.
& (Join-Path $bin 'windeployqt6.exe') --no-translations --no-system-d3d-compiler --no-opengl-sw `
  (Join-Path $Out 'syn-shell.exe') (Join-Path $Out 'syn-shell-attach.exe')
if ($LASTEXITCODE) { throw 'windeployqt failed' }

# Everything else: follow the imports of every exe and DLL (plugins too)
# and copy in whatever MSYS2's bin has, until nothing new turns up.
$objdump = Join-Path $bin 'objdump.exe'
$seen = @{}
$queue = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem $Out -Recurse -Include *.exe, *.dll | ForEach-Object { $queue.Enqueue($_.FullName); $seen[$_.Name.ToLower()] = $true }
while ($queue.Count) {
  $file = $queue.Dequeue()
  foreach ($line in & $objdump -p $file) {
    if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
    $dll = $Matches[1]
    if ($seen[$dll.ToLower()]) { continue }
    $seen[$dll.ToLower()] = $true
    $src = Join-Path $bin $dll
    if (Test-Path $src) {   # not there: Windows' own
      Copy-Item $src $Out
      $queue.Enqueue((Join-Path $Out $dll))
    }
  }
}

if ($Zip) {
  $zipPath = "$Out.zip"
  if (Test-Path $zipPath) { Remove-Item $zipPath }
  Compress-Archive -Path (Join-Path $Out '*') -DestinationPath $zipPath
  "Packed $zipPath"
}
"SYN-SHELL is in $Out (run syn-shell.exe)"
