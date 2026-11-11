# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

#Requires -Version 7
param(
    [switch]$Fix,
    [string[]]$Files,
    [string]$Preset = 'windows-debug',
    [int]$Jobs = [Environment]::ProcessorCount
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'llvm.ps1')

$clangTidy = Find-LlvmTool 'clang-tidy'
$buildDir = Join-Path $repoRoot "build\$Preset"
$database = Join-Path $buildDir 'compile_commands.json'
if (-not (Test-Path $database)) {
    throw "No compile_commands.json in $buildDir. Configure first: cmake --preset $Preset"
}

$ours = '^' + [regex]::Escape($repoRoot) + '[\\/](src|tests)[\\/]'
$units = (Get-Content $database -Raw | ConvertFrom-Json).file |
    ForEach-Object { [IO.Path]::GetFullPath($_) } |
    Where-Object { $_ -match $ours } |
    Sort-Object -Unique
if ($Files) {
    $wanted = $Files | ForEach-Object { (Resolve-Path $_).Path }
    $units = $units | Where-Object { $_ -in $wanted }
}
if (-not $units) {
    throw 'No matching translation units in compile_commands.json.'
}

$throttle = if ($Fix) { 1 } else { $Jobs }
$fixArgs = if ($Fix) { @('--fix') } else { @() }
Write-Host "clang-tidy on $(@($units).Count) files, $throttle at a time"

$outputs = $units | ForEach-Object -ThrottleLimit $throttle -Parallel {
    (& $using:clangTidy -p $using:buildDir --quiet @using:fixArgs $_ 2>&1 | Out-String)
}

$diagnostics = [ordered]@{}
foreach ($output in $outputs) {
    $current = $null
    foreach ($line in $output -split "`r?`n") {
        if ($line -match '^\S.*:\d+:\d+: (warning|error):') {
            $current = $line
            if (-not $diagnostics.Contains($current)) {
                $diagnostics[$current] = [Collections.Generic.List[string]]::new()
            }
        } elseif ($current -and $line -notmatch '^\d+ warnings? generated|^Suppressed \d+ warnings|^Use -header-filter') {
            $diagnostics[$current].Add($line)
        }
    }
}

foreach ($key in $diagnostics.Keys) {
    Write-Host $key
    $diagnostics[$key] | Where-Object { $_ } | ForEach-Object { Write-Host $_ }
}
if ($diagnostics.Count -gt 0) {
    Write-Host "$($diagnostics.Count) findings."
    exit 1
}
Write-Host 'No findings.'
