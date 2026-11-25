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

smoke_root="${RUNNER_TEMP:-/tmp}/kalve137-linux-package"
rm -rf "$smoke_root"
mkdir -p "$smoke_root"
unzip -q "$dest" -d "$smoke_root"
top="$(find "$smoke_root" -maxdepth 1 -mindepth 1 -type d | head -n 1)"
if [[ -z "$top" ]]; then
    echo "The zip has no top-level folder" >&2
    exit 1
fi

notice_for_name() {
    case "$1" in
        libQt* | Qt*) echo Qt ;;
        qwindows* | qsvg* | qgif* | qico* | qjpeg* | qmodernwindows* | qsvgicon*) echo Qt ;;
        libTK* | TK*) echo OpenCASCADE ;;
        libplanegcs* | planegcs*) echo PlaneGCS ;;
        libfreetype* | freetype*) echo FreeType ;;
        libpng* | libpng16*) echo libpng ;;
        libz.so* | libz-*) echo zlib ;;
        libbz2* | bz2*) echo bzip2 ;;
        libbrotli* | brotli*) echo Brotli ;;
        libfmt* | fmt*) echo fmt ;;
        libminiz* | miniz*) echo miniz ;;
        libspdlog* | spdlog*) echo spdlog ;;
        libboost_* | boost_*) echo Boost ;;
        *) return 1 ;;
    esac
}

should_skip_lib() {
    case "$1" in
        libc.so* | libm.so* | libpthread.so* | libdl.so* | librt.so* | libstdc++.so* | libgcc_s.so* \
        | libGL.so* | libEGL.so* | libX11.so* | libxcb.so* | libXau.so* | libXdmcp.so* | libbsd.so* \
        | ld-linux-x86-64.so*) return 0 ;;
    esac
    return 1
}

lib_base() {
    local file="$1"
    local base
    base="$(basename "$file")"
    base="${base%%.so*}"
    printf '%s\n' "$base"
}

bin="$top/bin"
notices_dir="$bin/notices"
missing=0
for file in bin/THIRD-PARTY-NOTICES.md bin/samples/dodecahedron.k137; do
    if [[ ! -f "$top/$file" ]]; then
        echo "The package is missing $top/$file" >&2
        missing=1
    fi
done

required=()
while IFS= read -r base; do
    [[ -z "$base" ]] && continue
    if notice="$(notice_for_name "$base")"; then
        required+=("$notice")
    else
        echo "No notice mapping for $base" >&2
        missing=1
    fi
done < <(
    find "$top" -name '*.so*' -type f | while IFS= read -r path; do
        base="$(lib_base "$path")"
        if should_skip_lib "$(basename "$path")"; then
            continue
        fi
        printf '%s\n' "$base"
    done | sort -u
)

for notice in Kalve137 PlaneGCS occt-qt-tools tinybvh Roboto Boost Eigen nlohmann-json; do
    required+=("$notice")
done

for notice in $(printf '%s\n' "${required[@]}" | sort -u); do
    if [[ ! -f "$notices_dir/${notice}.txt" ]]; then
        echo "The package is missing $notices_dir/${notice}.txt" >&2
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    exit 1
fi
echo "Package verified"
