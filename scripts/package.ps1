# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$Preset = 'windows-release',
    [switch]$SkipBuild,
    [string]$OpenGlDir,
    [int]$SmokeTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\$Preset"
$packageDir = Join-Path $build 'package'

function Invoke-Checked([string]$what, [scriptblock]$command) {
    & $command
    if ($LASTEXITCODE -ne 0) {
        throw "$what failed with exit code $LASTEXITCODE"
    }
}

if (-not $env:VSCMD_VER) {
    . (Join-Path $PSScriptRoot 'dev-shell.ps1') | Out-Null
}

Push-Location $root
try {
    if (-not $SkipBuild) {
        Invoke-Checked 'Configure' { cmake --preset $Preset }
        Invoke-Checked 'Build' { cmake --build --preset $Preset --target kalve137 }
    }
    if (Test-Path $packageDir) {
        Remove-Item -Recurse -Force $packageDir
    }
    Invoke-Checked 'CPack' { cpack --config (Join-Path $build 'CPackConfig.cmake') -B $packageDir }
} finally {
    Pop-Location
}

$zip = Get-ChildItem -Path $packageDir -Filter 'kalve137-*.zip' | Select-Object -First 1
if (-not $zip) {
    throw "CPack wrote no zip to $packageDir"
}
Write-Host ("Package {0}, {1:N1} MB" -f $zip.FullName, ($zip.Length / 1MB))

$unpacked = Join-Path ([IO.Path]::GetTempPath()) ("kalve137-package-" + [Guid]::NewGuid().ToString('N'))
Expand-Archive -Path $zip.FullName -DestinationPath $unpacked
try {
    $top = Join-Path $unpacked $zip.BaseName
    $bin = Join-Path $top 'bin'
    $expected = @(
        'bin\kalve137.exe'
        'bin\THIRD-PARTY-NOTICES.md'
        'bin\fonts\roboto\Roboto-Regular.ttf'
        'bin\fonts\roboto\Roboto-Medium.ttf'
        'bin\vcruntime140.dll'
        'bin\msvcp140.dll'
        'bin\samples\dodecahedron.k137'
        'plugins\platforms\qwindows.dll'
        'plugins\imageformats\qsvg.dll'
    )
    $missing = $expected | Where-Object { -not (Test-Path (Join-Path $top $_)) }
    if ($missing) {
        throw "Package is missing: $($missing -join ', ')"
    }

    function SkipPackagedDll([string]$name) {
        return $name -match '^(vcruntime|msvcp|concrt)\d'
    }

    function NoticeForDll([string]$relative) {
        if ($relative -match '^plugins[\\/]') {
            return 'Qt'
        }
        $base = [IO.Path]::GetFileNameWithoutExtension($relative)
        switch -Regex ($base) {
            '^Qt6' { return 'Qt' }
            '^TK' { return 'OpenCASCADE' }
            '^planegcs$' { return 'PlaneGCS' }
            '^freetype' { return 'FreeType' }
            '^libpng' { return 'libpng' }
            '^z$' { return 'zlib' }
            '^bz2$' { return 'bzip2' }
            '^brotli' { return 'Brotli' }
            '^fmt$' { return 'fmt' }
            '^miniz$' { return 'miniz' }
            '^spdlog$' { return 'spdlog' }
            default { return $null }
        }
    }

    $noticesDir = Join-Path $bin 'notices'
    $requiredNotices = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $unmapped = @()
    foreach ($dll in Get-ChildItem -Path $top -Recurse -Filter '*.dll') {
        if (SkipPackagedDll $dll.Name) {
            continue
        }
        $relative = $dll.FullName.Substring($top.Length + 1)
        $notice = NoticeForDll $relative
        if ($notice) {
            [void]$requiredNotices.Add($notice)
        } else {
            $unmapped += $relative
        }
    }
    foreach ($name in @(
            'Kalve137', 'PlaneGCS', 'occt-qt-tools', 'tinybvh', 'Roboto',
            'Boost', 'Eigen', 'nlohmann-json'
        )) {
        [void]$requiredNotices.Add($name)
    }
    if ($unmapped) {
        throw "Package has DLLs with no notice mapping: $($unmapped -join ', ')"
    }
    $missingNotices = $requiredNotices | Where-Object { -not (Test-Path (Join-Path $noticesDir "$_.txt")) }
    if ($missingNotices) {
        throw "Package is missing license notices: $($missingNotices -join ', ')"
    }
    $unwanted = 'bin\kalve137.pdb', 'bin\vc_redist.x64.exe', 'bin\opengl32sw.dll', 'bin\opengl32.dll', 'translations'
    $unwanted += 'plugins\tls', 'plugins\networkinformation', 'plugins\generic\qtuiotouchplugin.dll'
    $unwanted += Get-ChildItem -Path $top -Recurse -Include 'Qt6Network*', 'Qt6ShaderTools*', 'qsb*' |
        ForEach-Object { $_.FullName.Substring($top.Length + 1) }
    $present = $unwanted | Where-Object { Test-Path (Join-Path $top $_) }
    if ($present) {
        throw "Package carries files it should not: $($present -join ', ')"
    }
    $qtConf = Join-Path $bin 'qt.conf'
    if ((Test-Path $qtConf) -and ((Get-Content $qtConf -Raw) -match '[A-Za-z]:[\\/]')) {
        throw "qt.conf in the package points at an absolute path"
    }
    Write-Host "All $($expected.Count) expected files are present"

    if ($OpenGlDir) {
        Copy-Item -Path (Join-Path $OpenGlDir '*.dll') -Destination $bin
        Write-Host "Smoke runs with the OpenGL DLLs from $OpenGlDir"
    }

    $smokeOut = Join-Path $unpacked 'smoke'
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $bin 'kalve137.exe'
    $start.Arguments = '--smoke "{0}" "{1}"' -f (Join-Path $bin 'samples\dodecahedron.k137'), $smokeOut
    $start.WorkingDirectory = $unpacked
    $start.UseShellExecute = $false
    $keep = 'SystemRoot', 'SystemDrive', 'WINDIR', 'TEMP', 'TMP', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'COMPUTERNAME', 'USERNAME'
    $start.EnvironmentVariables.Clear()
    foreach ($name in $keep) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) {
            $start.EnvironmentVariables[$name] = $value
        }
    }
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = [System.Diagnostics.Process]::Start($start)
    if (-not $process.WaitForExit($SmokeTimeoutSeconds * 1000)) {
        $process.Kill()
        throw "Smoke run did not finish within $SmokeTimeoutSeconds seconds"
    }
    $report = Join-Path $smokeOut 'smoke.txt'
    if (Test-Path $report) {
        Get-Content $report | ForEach-Object { Write-Host "  $_" }
    }
    if ($process.ExitCode -ne 0) {
        throw "Smoke run failed with exit code $($process.ExitCode)"
    }
    Write-Host "Package verified"
} finally {
    Remove-Item -Recurse -Force $unpacked -ErrorAction SilentlyContinue
}
