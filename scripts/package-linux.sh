#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
preset="${PRESET:-linux-release}"
build="$root/build/$preset"
package_dir="$build/package"
skip_build=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) skip_build=1 ;;
        --preset) preset="$2"; build="$root/build/$preset"; shift ;;
        -h | --help) exit 2 ;;
        *) echo "Unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$skip_build" -eq 0 ]]; then
    cmake --preset "$preset"
    cmake --build --preset "$preset" --target kalve137
fi

rm -rf "$package_dir"
mkdir -p "$package_dir"
cpack --config "$build/CPackConfig.cmake" -B "$package_dir"

zip="$(find "$package_dir" -maxdepth 1 -name 'kalve137-*.zip' | head -n 1)"
if [[ -z "$zip" ]]; then
    echo "CPack wrote no zip to $package_dir" >&2
    exit 1
fi

dest="$package_dir/kalve137-linux-x64.zip"
cp -f "$zip" "$dest"
size="$(du -h "$dest" | awk '{print $1}')"
echo "Package $dest ($size)"
