#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
preset="${PRESET:-macos-release}"
build="$root/build/$preset"
package_dir="$build/package"
staging="$package_dir/staging"
skip_build=0
smoke_timeout=120

usage() {
    echo "Usage: package-macos.sh [--skip-build] [--preset macos-release]"
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) skip_build=1 ;;
        --preset) preset="$2"; build="$root/build/$preset"; shift ;;
        -h | --help) usage ;;
        *) echo "Unknown option $1" >&2; usage ;;
    esac
    shift
done

if [[ "$skip_build" -eq 0 ]]; then
    cmake --preset "$preset"
    cmake --build --preset "$preset" --target kalve137
fi

if [[ -z "${QT_ROOT_DIR:-}" ]]; then
    echo "QT_ROOT_DIR is not set" >&2
    exit 1
fi

macdeployqt="$QT_ROOT_DIR/bin/macdeployqt"
if [[ ! -x "$macdeployqt" ]]; then
    echo "No macdeployqt at $macdeployqt" >&2
    exit 1
fi

rm -rf "$staging"
mkdir -p "$package_dir"
cmake --install "$build" --prefix "$staging"

app="$(find "$staging" -maxdepth 2 -name '*.app' -print -quit)"
if [[ -z "$app" ]]; then
    echo "Install wrote no .app bundle under $staging" >&2
    exit 1
fi

frameworks="$app/Contents/Frameworks"
macos_dir="$app/Contents/MacOS"
mkdir -p "$frameworks"

cache_value() {
    sed -n "s/^$1:[A-Z]*=//p" "$build/CMakeCache.txt"
}

vcpkg_installed="$(cache_value VCPKG_INSTALLED_DIR)"
vcpkg_triplet="$(cache_value VCPKG_TARGET_TRIPLET)"
if [[ -z "$vcpkg_installed" || -z "$vcpkg_triplet" ]]; then
    echo "No vcpkg install dir or triplet in $build/CMakeCache.txt" >&2
    exit 1
fi
lib_dirs=("$staging/lib" "$vcpkg_installed/$vcpkg_triplet/lib")
copied=0

is_macho() {
    file "$1" 2>/dev/null | grep -q 'Mach-O'
}

should_skip_dep() {
    case "$1" in
        @loader_path/* | @executable_path/* | @rpath/* | @*/*) return 0 ;;
        /usr/lib/* | /System/* | /Library/Developer/*) return 0 ;;
    esac
    return 1
}

is_bad_dep() {
    case "$1" in
        *vcpkg* | */build/* | */Users/runner/work/*) return 0 ;;
    esac
    return 1
}

list_deps() {
    otool -L "$1" | awk '/^\t/ {print $1}'
}

own_id() {
    otool -D "$1" | awk '!/:$/ && id == "" {id = $0} END {print id}'
}

is_leak() {
    case "$1" in
        @rpath/* | @executable_path/* | @loader_path/*) return 1 ;;
        /System/* | /usr/lib/*) return 1 ;;
        /*) return 0 ;;
    esac
    return 1
}

add_exec_rpath() {
    local target="$1"
    if ! otool -l "$target" | grep -q 'path @executable_path/../Frameworks'; then
        install_name_tool -add_rpath "@executable_path/../Frameworks" "$target"
    fi
}

bundle_rpath_dep() {
    local name="$1"
    local dest="$frameworks/$name"
    if [[ -e "$dest" ]]; then
        return
    fi
    local dir
    for dir in "${lib_dirs[@]}"; do
        if [[ -f "$dir/$name" ]]; then
            cp -f "$dir/$name" "$dest"
            chmod 755 "$dest"
            install_name_tool -id "@rpath/$name" "$dest"
            copied=1
            return
        fi
    done
}

fix_dependencies() {
    local target="$1"
    local deps=()
    local id
    id="$(own_id "$target")"
    while IFS= read -r line; do
        deps+=("$line")
    done < <(list_deps "$target")
    for dep in "${deps[@]}"; do
        if [[ "$dep" == "$id" ]]; then
            continue
        fi
        if [[ "$dep" == @rpath/* ]]; then
            bundle_rpath_dep "${dep#@rpath/}"
            continue
        fi
        if should_skip_dep "$dep"; then
            continue
        fi
        if [[ ! -f "$dep" ]]; then
            if is_bad_dep "$dep"; then
                echo "Missing dependency $dep for $target" >&2
                exit 1
            fi
            continue
        fi
        if ! is_bad_dep "$dep"; then
            continue
        fi
        local name
        name="$(basename "$dep")"
        local dest="$frameworks/$name"
        if [[ ! -f "$dest" ]]; then
            cp -f "$dep" "$dest"
            chmod 755 "$dest"
            install_name_tool -id "@rpath/$name" "$dest"
        fi
        install_name_tool -change "$dep" "@rpath/$name" "$target"
    done
}

list_machos() {
    find "$app" -type f | while IFS= read -r path; do
        if is_macho "$path"; then
            printf '%s\n' "$path"
        fi
    done
}

bundle_dylibs() {
    local pass=0
    local changed=1
    while [[ "$changed" -eq 1 && "$pass" -lt 32 ]]; do
        changed=0
        pass=$((pass + 1))
        while IFS= read -r macho; do
            [[ -z "$macho" ]] && continue
            local before
            before="$(otool -L "$macho")"
            fix_dependencies "$macho"
            if [[ "$(otool -L "$macho")" != "$before" || "$copied" -eq 1 ]]; then
                changed=1
                copied=0
            fi
        done < <(list_machos)
    done
}

deploy_args=("$app" -verbose=1 -always-overwrite -no-strip)
for dir in "${lib_dirs[@]}"; do
    if [[ -d "$dir" ]]; then
        deploy_args+=("-libpath=$dir")
    fi
done
deploy_log="$package_dir/macdeployqt.log"
"$macdeployqt" "${deploy_args[@]}" 2>&1 | tee "$deploy_log"
deploy_errors="$(grep -c '^ERROR' "$deploy_log" || true)"
if [[ "$deploy_errors" -ne 0 && -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::warning title=macdeployqt::$deploy_errors ERROR lines from macdeployqt, see the Package step"
fi
rm -f "$app/Contents/PlugIns/generic/libqtuiotouchplugin.dylib"
bundle_dylibs
add_exec_rpath "$macos_dir/kalve137"

fonts="$app/Contents/Resources/fonts/roboto"
for font in Roboto-Regular.ttf Roboto-Medium.ttf; do
    if [[ ! -f "$fonts/$font" ]]; then
        echo "The bundle is missing $fonts/$font" >&2
        exit 1
    fi
done
if [[ -e "$macos_dir/fonts" ]]; then
    echo "Fonts were installed outside $app/Contents/Resources" >&2
    exit 1
fi

resources="$app/Contents/Resources"
notices_dir="$resources/notices"

notice_for_name() {
    case "$1" in
        Qt* | libQt*) echo Qt ;;
        libTK* | TK*) echo OpenCASCADE ;;
        libplanegcs* | planegcs*) echo PlaneGCS ;;
        libfreetype* | freetype*) echo FreeType ;;
        libpng* | libpng16*) echo libpng ;;
        libz.* | libz | libzlib*) echo zlib ;;
        libbz2* | bz2*) echo bzip2 ;;
        libbrotli* | brotli*) echo Brotli ;;
        libfmt* | fmt*) echo fmt ;;
        libminiz* | miniz*) echo miniz ;;
        libspdlog* | spdlog*) echo spdlog ;;
        libboost_* | boost_*) echo Boost ;;
        *) return 1 ;;
    esac
}

shipped_name() {
    local path="$1"
    local base
    base="$(basename "$path")"
    if [[ "$base" == *.framework ]]; then
        printf '%s\n' "${base%.framework}"
        return
    fi
    if [[ "$base" == *.dylib ]]; then
        printf '%s\n' "${base%.dylib}"
        return
    fi
    printf '%s\n' "$base"
}

static_notices=(
    Kalve137 PlaneGCS occt-qt-tools tinybvh Roboto Boost Eigen nlohmann-json
)

missing=0
for file in THIRD-PARTY-NOTICES.md samples/dodecahedron.k137; do
    if [[ ! -f "$resources/$file" ]]; then
        echo "The bundle is missing $resources/$file" >&2
        missing=1
    fi
done

plugins="$app/Contents/PlugIns"
required_notices=()
if [[ -d "$plugins" ]]; then
    required_notices+=(Qt)
fi
while IFS= read -r base; do
    [[ -z "$base" ]] && continue
    if notice="$(notice_for_name "$base")"; then
        required_notices+=("$notice")
    else
        echo "No notice mapping for $base" >&2
        missing=1
    fi
done < <(
    find "$app" -path "$plugins" -prune -o \( -name '*.dylib' -o -name '*.framework' \) -print | while IFS= read -r path; do
        shipped_name "$path"
    done | sort -u
)

for notice in "${static_notices[@]}"; do
    required_notices+=("$notice")
done

for notice in $(printf '%s\n' "${required_notices[@]}" | sort -u); do
    if [[ ! -f "$notices_dir/${notice}.txt" ]]; then
        echo "The bundle is missing $notices_dir/${notice}.txt" >&2
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    exit 1
fi
if [[ -e "$staging/bin" ]]; then
    echo "Install wrote files beside the bundle in $staging/bin:" >&2
    find "$staging/bin" -print >&2
    exit 1
fi

leaks=0
while IFS= read -r macho; do
    [[ -z "$macho" ]] && continue
    id="$(own_id "$macho")"
    while IFS= read -r dep; do
        [[ -n "$id" && "$dep" == "$id" ]] && continue
        if [[ "$dep" == @rpath/* && ! -e "$frameworks/${dep#@rpath/}" ]]; then
            echo "Unbundled dependency in $macho: $dep" >&2
            leaks=$((leaks + 1))
        elif is_leak "$dep"; then
            echo "Dependency leak in $macho: $dep" >&2
            leaks=$((leaks + 1))
        fi
    done < <(list_deps "$macho")
done < <(list_machos)
if [[ "$leaks" -ne 0 ]]; then
    echo "$leaks dependency leaks in $app" >&2
    exit 1
fi

codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

zip="$package_dir/kalve137-macos-arm64.zip"
rm -f "$zip"
ditto -c -k --keepParent "$app" "$zip"
size="$(du -h "$zip" | awk '{print $1}')"
echo "Package $zip ($size)"

smoke_root="${RUNNER_TEMP:-/tmp}/kalve137-macos-smoke"
rm -rf "$smoke_root"
mkdir -p "$smoke_root"
ditto -x -k "$zip" "$smoke_root"
unpacked_app="$(find "$smoke_root" -maxdepth 2 -name '*.app' -print -quit)"
if [[ -z "$unpacked_app" ]]; then
    echo "The zip has no .app bundle" >&2
    exit 1
fi

sample="$unpacked_app/Contents/Resources/samples/dodecahedron.k137"
if [[ ! -f "$sample" ]]; then
    echo "The zip has no sample at $sample" >&2
    exit 1
fi

smoke_out="$smoke_root/out"
mkdir -p "$smoke_out"
unset DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_INSERT_LIBRARIES DYLD_FALLBACK_LIBRARY_PATH QT_QPA_PLATFORM
exe="$unpacked_app/Contents/MacOS/kalve137"
if ! "$exe" --smoke "$sample" "$smoke_out"; then
    echo "Smoke failed" >&2
    exit 1
fi
echo "Package verified"
