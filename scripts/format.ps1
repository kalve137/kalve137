# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

#Requires -Version 7
param(
    [switch]$Check,
    [string[]]$Files
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'llvm.ps1')

$clangFormat = Find-LlvmTool 'clang-format'
$sources = @(Get-CppSources $Files 'src', 'tests')

if (-not $Check) {
    & $clangFormat -i --style=file $sources
    if ($LASTEXITCODE -ne 0) {
        throw 'clang-format failed.'
    }
    Write-Host "Formatted $($sources.Count) files."
    return
}

$unformatted = $sources | Where-Object {
    & $clangFormat --dry-run --Werror --style=file $_ 2>$null
    $LASTEXITCODE -ne 0
}
if ($unformatted) {
    $unformatted | ForEach-Object { Write-Host ([IO.Path]::GetRelativePath($repoRoot, $_)) }
    Write-Host "$(@($unformatted).Count) of $($sources.Count) files need formatting. Run .\scripts\format.ps1"
    exit 1
}
Write-Host "All $($sources.Count) files are formatted."
