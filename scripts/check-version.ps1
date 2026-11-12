# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

#Requires -Version 7
param(
    [string]$Tag
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent

$versionPattern = '(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)'
$failures = [System.Collections.Generic.List[string]]::new()

function ConvertTo-Version([string]$Text) {
    if ($Text -notmatch "^$versionPattern(?:-rc\.([1-9]\d*))?$") {
        return $null
    }
    $candidate = if ($Matches[4]) { [int]$Matches[4] } else { [int]::MaxValue }
    [pscustomobject]@{
        Core = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
        Line = "$($Matches[1]).$($Matches[2])"
        Key = [long[]]@([long]$Matches[1], [long]$Matches[2], [long]$Matches[3], $candidate)
    }
}

function Compare-Version($Left, $Right) {
    for ($i = 0; $i -lt 4; $i++) {
        if ($Left.Key[$i] -ne $Right.Key[$i]) {
            return [Math]::Sign($Left.Key[$i] - $Right.Key[$i])
        }
    }
    0
}

$cmake = Get-Content (Join-Path $repoRoot 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(kalve137\s+VERSION\s+(\S+)') {
    throw 'CMakeLists.txt has no project(kalve137 VERSION ...).'
}
$projectText = $Matches[1]
$project = ConvertTo-Version $projectText
if (-not $project -or $projectText -match '-') {
    $failures.Add("CMake version $projectText is not X.Y.Z.")
}

$released = [System.Collections.Generic.List[object]]::new()
$headings = Select-String -Path (Join-Path $repoRoot 'CHANGELOG.md') -Pattern '^## '
foreach ($heading in $headings) {
    $text = $heading.Line
    if ($text -eq '## [Unreleased]') {
        if ($released.Count -gt 0) {
            $failures.Add("CHANGELOG.md line $($heading.LineNumber): Unreleased must come first.")
        }
        continue
    }
    if ($text -notmatch "^## \[($versionPattern)\] - (\d{4}-\d{2}-\d{2})$") {
        $failures.Add("CHANGELOG.md line $($heading.LineNumber): '$text' is not '## [X.Y.Z] - YYYY-MM-DD'.")
        continue
    }
    $version = ConvertTo-Version $Matches[1]
    if ($released.Count -gt 0 -and (Compare-Version $version $released[$released.Count - 1]) -ge 0) {
        $failures.Add("CHANGELOG.md line $($heading.LineNumber): $($version.Core) is not older than the section above.")
    }
    $released.Add($version)
}

if ($project -and $released.Count -gt 0 -and (Compare-Version $project $released[0]) -lt 0) {
    $failures.Add("CMake version $($project.Core) is older than the newest release $($released[0].Core) in CHANGELOG.md.")
}

if ($Tag) {
    $tagVersion = if ($Tag.StartsWith('v')) { ConvertTo-Version $Tag.Substring(1) }
    if (-not $tagVersion) {
        $failures.Add("Tag $Tag is not vX.Y.Z or vX.Y.Z-rc.N.")
    }
    else {
        if ($project -and $tagVersion.Core -ne $project.Core) {
            $failures.Add("Tag $Tag does not match CMake version $($project.Core).")
        }
        if (-not ($released | Where-Object Core -EQ $tagVersion.Core)) {
            $failures.Add("CHANGELOG.md has no dated section for $($tagVersion.Core).")
        }
        $newer = git -C $repoRoot tag --list 'v*' | Where-Object { $_ -ne $Tag } | ForEach-Object {
            $existing = ConvertTo-Version $_.Substring(1)
            if ($existing -and $existing.Line -eq $tagVersion.Line -and (Compare-Version $existing $tagVersion) -ge 0) {
                $_
            }
        }
        if ($newer) {
            $failures.Add("Tag $Tag is not newer than $($newer -join ', ').")
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host "Version $($project.Core) is consistent$(if ($Tag) { " with tag $Tag" })."
