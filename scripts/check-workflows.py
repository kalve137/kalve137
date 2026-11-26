#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

import re
import sys
from pathlib import Path

workflows = Path(__file__).resolve().parent.parent / ".github" / "workflows"
checkout = re.compile(r"^\s*- uses: actions/checkout@")
pinned_ref = re.compile(r"^\s+ref: \$\{\{ inputs\.ref \}\}\s*$")
caller_context = re.compile(r"github\.(sha|ref|event_name)")
vcpkg_cache_step = re.compile(r"^\s*- name: .*vcpkg binary cache")
vcpkg_env = re.compile(r"^\s*VCPKG_\w+: ")


def read_lines(path):
    return path.read_text(encoding="utf-8").splitlines()


def indent_of(line):
    return len(line) - len(line.lstrip())


def body(lines, number):
    indent = indent_of(lines[number])
    taken = []
    for following in lines[number + 1 :]:
        if following.strip() and indent_of(following) <= indent:
            break
        taken.append(following)
    return taken


def section(lines, header):
    for number, line in enumerate(lines):
        if line.rstrip() == header:
            return body(lines, number)
    return []


def checkouts_without_ref(path):
    lines = read_lines(path)
    missing = []
    for number, line in enumerate(lines):
        if checkout.match(line) and not any(pinned_ref.match(entry) for entry in body(lines, number)):
            missing.append(number + 1)
    return missing


def caller_context_uses(path):
    lines = read_lines(path)
    return [number + 1 for number, line in enumerate(lines) if caller_context.search(line)]


def vcpkg_cache_setup(path, job):
    lines = read_lines(path)
    job_lines = section(lines, f"  {job}:")
    env = [line.strip() for line in section(lines, "env:") + section(job_lines, "    env:") if vcpkg_env.match(line)]
    steps = [
        "\n".join([line, *body(job_lines, number)]).rstrip()
        for number, line in enumerate(job_lines)
        if vcpkg_cache_step.match(line)
    ]
    return env, steps


failures = []
for number in checkouts_without_ref(workflows / "packages.yml"):
    failures.append(f"packages.yml:{number}: actions/checkout lacks ref: ${{{{ inputs.ref }}}}")
for number in caller_context_uses(workflows / "nightly-build.yml"):
    failures.append(f"nightly-build.yml:{number}: reads github.sha, github.ref or github.event_name of the caller")
for job in ("windows", "linux", "macos"):
    ci_env, ci_steps = vcpkg_cache_setup(workflows / "ci.yml", job)
    nightly_env, nightly_steps = vcpkg_cache_setup(workflows / "packages.yml", job)
    if not ci_steps:
        failures.append(f"ci.yml: {job} has no vcpkg binary cache steps")
    if ci_env != nightly_env:
        failures.append(f"packages.yml: {job} VCPKG_ env differs from ci.yml")
    if ci_steps != nightly_steps:
        failures.append(f"packages.yml: {job} vcpkg binary cache steps differ from ci.yml")

for failure in failures:
    print(failure, file=sys.stderr)
if failures:
    sys.exit(1)
print("Workflows check out the planned commit and share the vcpkg cache with CI")
