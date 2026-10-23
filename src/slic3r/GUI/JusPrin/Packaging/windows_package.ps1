# Makes JusPrin's Windows installer and portable zip from a finished build.
#
#   windows_package.ps1 -BuildDir build -Version 2.0.0 -Arch x64 -OutDir dist
#
# The installer comes from OrcaSlicer's CPack setup, whose name, version and
# uninstall key packaging.cmake replaces with JusPrin's. The portable zip is
# OrcaSlicer's install folder (build\OrcaSlicer) under JusPrin's name.
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Arch,
    [Parameter(Mandatory)] [string] $OutDir,
    [switch] $SkipInstaller
)
$ErrorActionPreference = 'Stop'

$BuildDir = (Resolve-Path $BuildDir).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$installer = "JusPrin_Windows_Installer_V${Version}_${Arch}.exe"
$portable = "JusPrin_Windows_V${Version}_${Arch}_portable.zip"

if (-not $SkipInstaller) {
    Push-Location $BuildDir
    try {
        cpack -G NSIS
        if ($LASTEXITCODE -ne 0) { throw "cpack failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
    $built = Join-Path $BuildDir $installer
    if (-not (Test-Path $built)) {
        throw "CPack did not write $installer. Check that packaging.cmake's version matches $Version."
    }
    Copy-Item $built (Join-Path $OutDir $installer)
}

# Zip the install folder as JusPrin\..., the layout JusPrin 1.x's zip had.
$stage = Join-Path ([IO.Path]::GetTempPath()) ("jusprin-portable-" + [Guid]::NewGuid())
New-Item -ItemType Directory $stage | Out-Null
try {
    Copy-Item (Join-Path $BuildDir 'OrcaSlicer') (Join-Path $stage 'JusPrin') -Recurse
    $zip = Join-Path $OutDir $portable
    if (Test-Path $zip) { Remove-Item $zip }
    # 7-Zip as OrcaSlicer's workflow uses it; Compress-Archive where it is missing.
    $sevenZip = 'C:\Program Files\7-Zip\7z.exe'
    if (Test-Path $sevenZip) {
        & $sevenZip a -tzip $zip (Join-Path $stage 'JusPrin') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7z failed with exit code $LASTEXITCODE" }
    } else {
        Compress-Archive -Path (Join-Path $stage 'JusPrin') -DestinationPath $zip
    }
} finally {
    Remove-Item $stage -Recurse -Force
}

Get-ChildItem $OutDir | Select-Object Name, Length
