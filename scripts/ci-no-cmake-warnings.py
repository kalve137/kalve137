#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

import re
import subprocess
import sys

WARNING = re.compile(rb"^CMake (Deprecation )?Warning")
VCPKG_START = b"-- Running vcpkg install"
VCPKG_END = b"-- Running vcpkg install - "

if len(sys.argv) < 2:
    sys.exit("Usage: ci-no-cmake-warnings.py <command> [arguments]")

process = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
warnings = []
in_vcpkg = False
for line in process.stdout:
    sys.stdout.buffer.write(line)
    sys.stdout.buffer.flush()
    stripped = line.rstrip()
    if stripped.startswith(VCPKG_END):
        in_vcpkg = False
    elif stripped == VCPKG_START:
        in_vcpkg = True
    elif not in_vcpkg and WARNING.match(stripped):
        warnings.append(stripped.decode(errors="replace"))
status = process.wait()

for warning in warnings:
    print(f"::error title=CMake warning::{warning.replace('%', '%25')}", flush=True)
if warnings:
    print(f"{len(warnings)} CMake warnings outside the vcpkg install, see the lines above", file=sys.stderr)
    sys.exit(status or 1)
sys.exit(status)
