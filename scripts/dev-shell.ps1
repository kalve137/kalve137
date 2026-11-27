# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw 'No Visual Studio installation with the C++ x64 toolset was found.'
}

$ownVcpkg = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User') }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

if ($ownVcpkg) {
    $env:VCPKG_ROOT = $ownVcpkg
}
if (-not $env:QT_ROOT_DIR) {
    $env:QT_ROOT_DIR = [Environment]::GetEnvironmentVariable('QT_ROOT_DIR', 'User')
}

$cmakeDir = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$cmakeBin = if ($env:KALVE137_CMAKE_DIR) { $env:KALVE137_CMAKE_DIR } else { "$cmakeDir\CMake\bin" }
$env:PATH = "$cmakeBin;$cmakeDir\Ninja;$env:PATH"

Write-Host "Kalve 137 dev shell: VCPKG_ROOT=$env:VCPKG_ROOT QT_ROOT_DIR=$env:QT_ROOT_DIR"
