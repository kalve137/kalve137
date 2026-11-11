# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

$repoRoot = Split-Path $PSScriptRoot -Parent

function Find-LlvmTool([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }
    $installed = Join-Path $env:ProgramFiles "LLVM\bin\$Name.exe"
    if (Test-Path $installed) {
        return $installed
    }
    throw "$Name was not found. Install LLVM from https://github.com/llvm/llvm-project/releases or put it on PATH."
}

function Get-CppSources([string[]]$Paths, [string[]]$Folders) {
    if ($Paths) {
        return $Paths | ForEach-Object { (Resolve-Path $_).Path }
    }
    return $Folders | ForEach-Object { Get-ChildItem (Join-Path $repoRoot $_) -Recurse -File -Include *.cpp, *.h } |
        ForEach-Object FullName
}
