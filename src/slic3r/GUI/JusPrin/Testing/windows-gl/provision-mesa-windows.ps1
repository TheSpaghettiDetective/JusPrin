# Copy the Mesa software-GL pair into a Windows build tree.
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
# Background, failure signatures and the open d3d12 defect:
# agent-docs/jusprin/headless-gl-handoff.md
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
    [Parameter(Mandatory = $true)][string]$Source
)

$ErrorActionPreference = 'Stop'

foreach ($relative in 'libgallium_wgl.dll', 'mesa\opengl32.dll') {
    if (-not (Test-Path (Join-Path $Source $relative))) {
        throw "Mesa source file missing: $(Join-Path $Source $relative)"
    }
}

# Harnesses that open a GL canvas need the pair as much as the application does.
$exeDirs = Get-ChildItem $BuildDir -Recurse -Filter '*.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'orca-slicer|_harness' } |
    Select-Object -ExpandProperty DirectoryName -Unique

if (-not $exeDirs) {
    Write-Warning "No GUI executables found under $BuildDir - build first, then re-run."
    return
}

foreach ($dir in $exeDirs) {
    Copy-Item (Join-Path $Source 'libgallium_wgl.dll') $dir -Force
    New-Item -ItemType Directory -Force (Join-Path $dir 'mesa') | Out-Null
    Copy-Item (Join-Path $Source 'mesa\opengl32.dll') (Join-Path $dir 'mesa') -Force
    Write-Output "provisioned $dir"
}

Write-Output 'Run the application with GALLIUM_DRIVER=llvmpipe; the d3d12 driver crashes on the first canvas render.'
