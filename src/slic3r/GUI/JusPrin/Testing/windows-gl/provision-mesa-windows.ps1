# Copy the Mesa software-GL set into a Windows build tree.
#
# A verification machine with no GPU reports system GL 1.1, so OrcaSlicer's
# launcher loads "<exe dir>\mesa\opengl32.dll" instead. Neither that stub nor the
# libgallium_wgl.dll it imports is produced by CMake or checked in, so every
# freshly configured build tree needs this step or the 3D canvas stays black.
#
# Placement is not interchangeable: libgallium_wgl.dll must sit beside the
# executable (Windows resolves a dependent DLL against the process directory,
# not the importing DLL's), and opengl32.dll must sit in the mesa\ subfolder
# (the launcher loads it by that exact relative path). Swapping them either
# leaves the stub unresolvable or shadows the system GL and kills the app at
# launch.
#
# dxil.dll also goes beside the executable. Mesa's d3d12 driver, which it selects
# by default on WARP, needs it to sign the shaders it translates; without it the
# first shader-backed draw kills the process with 0x80070057. Windows Server does
# not ship it, but every Windows SDK does, so it is taken from the SDK by default.
#
# Failure signatures: "Software GL on Windows machines without a GPU" in
# agent-docs/jusprin/engineering-method.md
#
# -Source is a directory holding an already-provisioned pair, i.e. containing
# both libgallium_wgl.dll and mesa\opengl32.dll. These binaries are not
# distributed with this repository; take them from another provisioned build
# tree or from an upstream Mesa Windows build.
#
#   pwsh provision-mesa-windows.ps1 -BuildDir C:\b\myBuild -Source C:\src\JusPrin\build\src\Release

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Dxil = (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist\D3D\x64\dxil.dll')
)

$ErrorActionPreference = 'Stop'

foreach ($relative in 'libgallium_wgl.dll', 'mesa\opengl32.dll') {
    if (-not (Test-Path (Join-Path $Source $relative))) {
        throw "Mesa source file missing: $(Join-Path $Source $relative)"
    }
}
if (-not (Test-Path $Dxil)) {
    throw "dxil.dll not found at $Dxil - install a Windows SDK or pass -Dxil."
}

# Harnesses that open a GL canvas need the set as much as the application does.
$exeDirs = Get-ChildItem $BuildDir -Recurse -Filter '*.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'orca-slicer|_harness' } |
    Select-Object -ExpandProperty DirectoryName -Unique

if (-not $exeDirs) {
    Write-Warning "No GUI executables found under $BuildDir - build first, then re-run."
    return
}

foreach ($dir in $exeDirs) {
    Copy-Item (Join-Path $Source 'libgallium_wgl.dll') $dir -Force
    Copy-Item $Dxil $dir -Force
    New-Item -ItemType Directory -Force (Join-Path $dir 'mesa') | Out-Null
    Copy-Item (Join-Path $Source 'mesa\opengl32.dll') (Join-Path $dir 'mesa') -Force
    Write-Output "provisioned $dir"
}
