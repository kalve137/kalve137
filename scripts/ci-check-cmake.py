#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

import os
import subprocess
import sys

expected = f"cmake version {os.environ['CMAKE_VERSION']}"
found = subprocess.run(["cmake", "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
if found != expected:
    sys.exit(f"::error title=CMake version::{found} on PATH, workflows pin {expected}")
print(found)
