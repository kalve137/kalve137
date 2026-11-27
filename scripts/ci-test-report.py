#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

SLOWEST = 20
LISTED = 10
SKIP_LINE = re.compile(r"[:)] Skipped$")
LOCATION = re.compile(r"^\S.*[:(]\d+\)?:?$")
TIMED_LINE = re.compile(r"^(?P<measured>.*\d+ ms), budget (?P<budget>\d+) ms$")


def escape(text):
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def cell(text):
    return text.replace("|", "\\|")


def name_of(case):
    name = case.get("name", "")
    suite = case.get("classname", "")
    return name if not suite or suite == name else f"{suite}.{name}"


def ctest_skip_reason(output):
    reason = []
    inside = False
    for line in output.splitlines():
        if inside and line.startswith("[  SKIPPED ]"):
            break
        if inside and line.strip():
            reason.append(line.strip())
        elif SKIP_LINE.search(line.rstrip()):
            inside = True
    return " ".join(reason)


def gtest_skip_reason(message):
    lines = [line.strip() for line in message.splitlines() if line.strip()]
    if lines and LOCATION.match(lines[0]):
        lines = lines[1:]
    return " ".join(lines)


def timed_lines(output):
    for line in output.splitlines():
        found = TIMED_LINE.match(line.strip())
        if found:
            yield found["measured"], int(found["budget"])


def cases_in(paths):
    for path in paths:
        files = sorted(path.glob("*.xml")) if path.is_dir() else [path] if path.is_file() else []
        for file in files:
            yield from ET.parse(file).getroot().iter("testcase")


def by_reason(entries):
    groups = defaultdict(list)
    for name, reason in entries:
        groups[reason].append(name)
    return sorted(groups.items(), key=lambda item: (-len(item[1]), item[0]))


parser = argparse.ArgumentParser(description="Summarize ctest JUnit or gtest XML results for the CI log and run page")
parser.add_argument("title")
parser.add_argument("results", nargs="+", type=Path)
parser.add_argument("--extra", type=Path, help="JSON with excluded tests and notes")
args = parser.parse_args()

extra = {}
if args.extra and args.extra.is_file():
    extra = json.loads(args.extra.read_text(encoding="utf-8"))
excluded = sorted(extra.get("excluded", {}).items())
notes = extra.get("notes", [])

passed = 0
failed = []
skipped = []
disabled = []
times = []
timed = []
for case in cases_in(args.results):
    name = name_of(case)
    timed += [(name, measured, budget) for measured, budget in timed_lines(case.findtext("system-out") or "")]
    status = case.get("status", "run")
    gtest = case.get("result") is not None
    failure = case.find("failure")
    if failure is None:
        failure = case.find("error")
    skip = case.find("skipped")
    if failure is not None or status == "fail":
        message = failure.get("message", "") if failure is not None else ""
        failed.append((name, " ".join(message.split()[:12]) or "failed"))
    elif status == "disabled" or (gtest and status == "notrun"):
        disabled.append((name, "disabled in the source"))
        continue
    elif skip is not None:
        if gtest:
            reason = gtest_skip_reason(skip.get("message", ""))
        else:
            reason = ctest_skip_reason(case.findtext("system-out") or "")
        skipped.append((name, reason or "no reason given"))
    else:
        passed += 1
    times.append((float(case.get("time") or 0.0), name))

total = passed + len(failed) + len(skipped) + len(disabled)
if total == 0:
    print(f"::error title={args.title}::No test results in {', '.join(map(str, args.results))}")
    sys.exit(1)

seconds = sum(time for time, _ in times)
headline = (f"{passed} passed, {len(failed)} failed, {len(skipped)} skipped, {len(disabled)} disabled and "
            f"{len(excluded)} excluded in {seconds:.0f} s of test time")
slowest = sorted(times, reverse=True)[:SLOWEST]

print(f"{args.title}: {headline}")
for note in notes:
    print(note)
print(f"Slowest {len(slowest)} of {len(times)}:")
for time, name in slowest:
    print(f"{time:9.2f} s  {name}")
for name, measured, budget in timed:
    print(f"timed {name}: {measured}, budget {budget} ms")
for label, entries in (("failed", failed), ("skipped", skipped), ("disabled", disabled), ("excluded", excluded)):
    for name, reason in entries:
        print(f"{label} {name}: {reason}")

for reason, names in by_reason(failed):
    shown = ", ".join(names[:LISTED]) + (", ..." if len(names) > LISTED else "")
    print(f"::error title={args.title}::{len(names)} failed ({escape(reason)}): {escape(shown)}")
for label, entries in (("skipped", skipped), ("disabled", disabled), ("excluded", excluded)):
    for reason, names in by_reason(entries):
        shown = ", ".join(names[:LISTED]) + (", ..." if len(names) > LISTED else "")
        print(f"::warning title={args.title}::{len(names)} {label}, {escape(reason)}: {escape(shown)}")

summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
if summary_path:
    lines = [f"### {args.title}", "", headline, ""]
    lines += [f"- {note}" for note in notes]
    if notes:
        lines.append("")
    not_run = [("failed", name, reason) for name, reason in failed]
    for label, entries in (("skipped", skipped), ("disabled", disabled), ("excluded", excluded)):
        not_run += [(label, name, reason) for name, reason in entries]
    if not_run:
        lines += ["| Result | Test | Reason |", "| --- | --- | --- |"]
        lines += [f"| {label} | `{name}` | {cell(reason)} |" for label, name, reason in not_run]
        lines.append("")
    if timed:
        lines += ["Timed tests", "", "| Test | Measured | Budget |", "| --- | --- | ---: |"]
        lines += [f"| `{name}` | {cell(measured)} | {budget} ms |" for name, measured, budget in timed]
        lines.append("")
    lines += [f"Slowest {len(slowest)} of {len(times)}", "", "| Seconds | Test |", "| ---: | --- |"]
    lines += [f"| {time:.2f} | `{name}` |" for time, name in slowest]
    lines.append("")
    with open(summary_path, "a", encoding="utf-8") as summary:
        summary.write("\n".join(lines) + "\n")
