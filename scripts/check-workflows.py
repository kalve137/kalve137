#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

import re
import sys
from pathlib import Path

workflows = Path(__file__).resolve().parent.parent / ".github" / "workflows"
checkout = re.compile(r"^(\s*)- uses: actions/checkout@")
pinned_ref = re.compile(r"^\s+ref: \$\{\{ inputs\.ref \}\}\s*$")
caller_context = re.compile(r"github\.(sha|ref|event_name)")


def checkouts_without_ref(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    missing = []
    for number, line in enumerate(lines):
        match = checkout.match(line)
        if not match:
            continue
        indent = len(match.group(1))
        step = []
        for following in lines[number + 1 :]:
            if following.strip() and len(following) - len(following.lstrip()) <= indent:
                break
            step.append(following)
        if not any(pinned_ref.match(entry) for entry in step):
            missing.append(number + 1)
    return missing


def caller_context_uses(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    return [number + 1 for number, line in enumerate(lines) if caller_context.search(line)]


failures = []
for number in checkouts_without_ref(workflows / "packages.yml"):
    failures.append(f"packages.yml:{number}: actions/checkout lacks ref: ${{{{ inputs.ref }}}}")
for number in caller_context_uses(workflows / "nightly-build.yml"):
    failures.append(f"nightly-build.yml:{number}: reads github.sha, github.ref or github.event_name of the caller")

for failure in failures:
    print(failure, file=sys.stderr)
if failures:
    sys.exit(1)
print("Workflows check out the planned commit")
