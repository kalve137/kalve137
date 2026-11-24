# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

#Requires -Version 7
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('count', 'save-plan', 'tail-start', 'tail-stop')]
    [string]$Command,
    [string]$Path,
    [string]$VcpkgRoot
)
$ErrorActionPreference = 'Stop'

$pidFile = Join-Path $env:RUNNER_TEMP 'vcpkg-log-tail.pid'

function Get-CacheFileCount([string]$Root) {
    if (-not (Test-Path $Root)) {
        return 0
    }
    return @(Get-ChildItem -Path $Root -Recurse -File -ErrorAction SilentlyContinue).Count
}

function Write-GithubOutput([string]$Name, [string]$Value) {
    if ($env:GITHUB_OUTPUT) {
        "${Name}=${Value}" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
}

switch ($Command) {
    'count' {
        if (-not $Path) {
            throw 'count requires -Path'
        }
        Write-GithubOutput 'count' (Get-CacheFileCount $Path)
    }
    'save-plan' {
        if (-not $Path) {
            throw 'save-plan requires -Path'
        }
        $countAfter = Get-CacheFileCount $Path
        $countBefore = [int]($env:COUNT_BEFORE ?? '0')
        $cacheHit = $env:CACHE_HIT ?? ''
        $primary = $env:CACHE_PRIMARY_KEY ?? ''
        $runId = $env:RUN_ID ?? ''
        $save = 'false'
        $key = $primary
        if ($countAfter -gt 0 -and $primary) {
            if ($cacheHit -ne 'true') {
                $save = 'true'
            }
            elseif ($countAfter -gt $countBefore) {
                $save = 'true'
                $key = "${primary}-${runId}"
            }
        }
        Write-GithubOutput 'save' $save
        Write-GithubOutput 'key' $key
        Write-GithubOutput 'count' "$countAfter"
    }
    'tail-start' {
        if (-not $VcpkgRoot) {
            throw 'tail-start requires -VcpkgRoot'
        }
        $script = @"
while (`$true) {
    `$logs = Get-ChildItem -Path '$VcpkgRoot/buildtrees' -Filter '*.log' -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (`$logs) {
        `$line = Get-Content -Path `$logs.FullName -Tail 1 -ErrorAction SilentlyContinue
        if (`$line) {
            Write-Host "vcpkg $($logs.Directory.Parent.Name): `$line"
        }
    }
    Start-Sleep -Seconds 60
}
"@
        $job = Start-Job -ScriptBlock ([scriptblock]::Create($script))
        $job.Id | Out-File -FilePath $pidFile -Encoding utf8
    }
    'tail-stop' {
        if (Test-Path $pidFile) {
            $jobId = Get-Content $pidFile -Raw
            $job = Get-Job -Id $jobId -ErrorAction SilentlyContinue
            if ($job) {
                Stop-Job $job -ErrorAction SilentlyContinue
                Remove-Job $job -Force -ErrorAction SilentlyContinue
            }
            Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
        }
    }
}