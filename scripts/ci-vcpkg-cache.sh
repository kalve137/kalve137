#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

command="${1:-}"
cache_path=""
vcpkg_root=""
pid_file="${RUNNER_TEMP:-/tmp}/vcpkg-log-tail.pid"

cache_file_count() {
    local root="$1"
    if [ ! -d "$root" ]; then
        echo 0
        return
    fi
    find "$root" -type f 2>/dev/null | wc -l | tr -d ' '
}

github_output() {
    local name="$1"
    local value="$2"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        printf '%s=%s\n' "$name" "$value" >>"$GITHUB_OUTPUT"
    fi
}

usage() {
    echo "usage: $0 count|save-plan|tail-start|tail-stop [-Path dir] [-VcpkgRoot dir]" >&2
    exit 1
}

shift || usage
while [ $# -gt 0 ]; do
    case "$1" in
        -Path)
            cache_path="$2"
            shift 2
            ;;
        -VcpkgRoot)
            vcpkg_root="$2"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

case "$command" in
    count)
        [ -n "$cache_path" ] || usage
        github_output count "$(cache_file_count "$cache_path")"
        ;;
    save-plan)
        [ -n "$cache_path" ] || usage
        count_after="$(cache_file_count "$cache_path")"
        count_before="${COUNT_BEFORE:-0}"
        cache_hit="${CACHE_HIT:-}"
        primary="${CACHE_PRIMARY_KEY:-}"
        run_id="${RUN_ID:-}"
        save=false
        key="$primary"
        if [ "$count_after" -gt 0 ] && [ -n "$primary" ]; then
            if [ "$cache_hit" != true ]; then
                save=true
            elif [ "$count_after" -gt "$count_before" ]; then
                save=true
                key="${primary}-${run_id}"
            fi
        fi
        github_output save "$save"
        github_output key "$key"
        github_output count "$count_after"
        ;;
    tail-start)
        [ -n "$vcpkg_root" ] || usage
        (
            while true; do
                latest=""
                if [ -d "$vcpkg_root/buildtrees" ]; then
                    while IFS= read -r candidate; do
                        [ -z "$candidate" ] && continue
                        if [ -z "$latest" ] || [ "$candidate" -nt "$latest" ]; then
                            latest="$candidate"
                        fi
                    done < <(find "$vcpkg_root/buildtrees" -name '*.log' -type f 2>/dev/null)
                fi
                if [ -n "$latest" ] && [ -f "$latest" ]; then
                    port="$(basename "$(dirname "$(dirname "$latest")")")"
                    line="$(tail -n 1 "$latest" 2>/dev/null || true)"
                    if [ -n "$line" ]; then
                        echo "vcpkg ${port}: ${line}"
                    fi
                fi
                sleep 60
            done
        ) &
        echo $! >"$pid_file"
        ;;
    tail-stop)
        if [ -f "$pid_file" ]; then
            kill "$(cat "$pid_file")" 2>/dev/null || true
            rm -f "$pid_file"
        fi
        ;;
    *)
        usage
        ;;
esac