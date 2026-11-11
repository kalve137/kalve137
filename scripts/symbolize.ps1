# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)][string]$Dump,
    [string[]]$Symbols = @(),
    [switch]$Analyze
)

$ErrorActionPreference = 'Stop'

function Find-Cdb {
    $command = Get-Command cdb.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $candidates = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
        "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\cdb.exe"
    )
    $winDbg = Get-AppxPackage -Name Microsoft.WinDbg -ErrorAction SilentlyContinue
    if ($winDbg) {
        $candidates += Join-Path $winDbg.InstallLocation 'amd64\cdb.exe'
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw 'cdb.exe not found. Install WinDbg (winget install Microsoft.WinDbg) or the Windows SDK debugging tools.'
}

$dumpPath = (Resolve-Path $Dump).Path
$root = Split-Path $PSScriptRoot -Parent

$paths = @($Symbols) + (Split-Path $dumpPath -Parent)
$paths += Get-ChildItem (Join-Path $root 'build\*\symbols\*') -Directory -ErrorAction SilentlyContinue |
    ForEach-Object FullName
$paths += Get-ChildItem (Join-Path $root 'build\*') -Directory -ErrorAction SilentlyContinue |
    ForEach-Object FullName

$cache = Join-Path $env:LOCALAPPDATA 'Temp\SymbolCache'
$symbolPath = (($paths | Select-Object -Unique) -join ';') + ";srv*$cache*https://msdl.microsoft.com/download/symbols"

$commands = if ($Analyze) { '!analyze -v; q' } else { '.ecxr; kb 40; q' }

& (Find-Cdb) -z $dumpPath -y $symbolPath -i $symbolPath -lines -c $commands
