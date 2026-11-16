# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

"""A scripted agent that writes parts, hits problems and repairs the source from kalvec's diagnostics.

Run from the repo root after building: python examples/agent_loop.py build/windows-release
"""

import json
import pathlib
import re
import subprocess
import sys

BUILD = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/windows-release")
KALVEC = BUILD / "kalvec.exe"
WORK = BUILD / "agent"

PLATE_DRAFT = """part plate
param width = 50 mm
param height = 34 mm
param thick = 5 mm
param inset = 4 mm
param bolt = M4
sketch base on XY {
  rect outline center (0 mm, 0 mm) size (width, height)
}
extrude body = base up thick
sketch mounts on body.outline.end {
  circle a center (-width / 2 + inset, -height / 2 + inset) diameter clearance(bolt)
  circle b center (width / 2 - inset, -height / 2 + inset) diameter clearance(bolt)
  circle c center (width / 2 - inset, height / 2 - inset) diameter clearance(bolt)
  circle d center (-width / 2 + inset, height / 2 - inset) diameter clearance(bolt)
}
cut holes = mounts through
check margin: distance(holes.a.wal, body.outline.west) >= 2.5 mm
check mass: volume() <= 9 cm3
"""

TAB_DRAFT = """part tab
param reach = 50 mm
param width = 20 mm
param thick = 4 mm
sketch outline on XY {
  point a
  point b near (40 mm, 0 mm)
  point c near (40 mm, 20 mm)
  point d near (0 mm, 20 mm)
  point e near (40 mm, 10 mm)
  line base from a to b
  arc nose from b to c center e
  line top from c to d
  line back from d to a
  at(a) = (0 mm, 0 mm)
  horizontal(base)
  horizontal(top)
  vertical(back)
  tangent(base, nose)
  tangent(nose, top)
  parallel(base, top)
  length(base) = reach - width / 2
  length(back) = width
}
extrude body = outline up thick
check long: size_x() == reach
"""


def kalvec(*args):
    result = subprocess.run([str(KALVEC), *map(str, args)], capture_output=True, text=True)
    return result.returncode, result.stdout


def say(text):
    print(f"agent: {text}")


def repair_selector(part, problem):
    match = re.search(r"Did you mean '([^']+)'", problem["hint"])
    path = re.search(r"'([^']+)' names no face", problem["message"])
    if not match or not path:
        say("no machine-readable fix in the hint, giving up")
        return False
    wrong = path.group(1)
    right = ".".join(wrong.split(".")[:-1] + [match.group(1)])
    part.write_text(part.read_text(encoding="utf-8").replace(wrong, right), encoding="utf-8")
    say(f"replaced {wrong} with {right}")
    return True


def repair_check(part, snapshot):
    failed = next(c for c in snapshot["checks"] if not c["passed"])
    say(f"check {failed['name']} measures {failed['value']['text']} against {failed['limit']['text']}")
    code, out = kalvec("suggest", part, "--json")
    options = json.loads(out)
    for option in options:
        say(
            f"suggest: {option['param']} = {option['value']} "
            f"(all checks pass: {option['all_checks_pass']}, {option['builds']} rebuilds)"
        )
    safe = [o for o in options if o["all_checks_pass"]]
    if not safe:
        say("no single param change keeps every check green, giving up")
        return False
    pick = safe[0]
    kalvec("set", part, pick["param"], pick["value"])
    say(f"kalvec set {pick['param']} {pick['value']}")
    return True


def repair_repeat(part, problem):
    listed = re.findall(r"'([^']+)'", problem["message"].split(":", 1)[1])
    suggested = re.search(r"like '([^']+)'", problem["hint"])
    if not listed or not suggested:
        say("the diagnostic names no constraint to drop, giving up")
        return False
    drop = suggested.group(1)
    source = part.read_text(encoding="utf-8")
    line = f"  {drop}\n"
    if line not in source:
        say(f"'{drop}' is not a line of the source, giving up")
        return False
    part.write_text(source.replace(line, "", 1), encoding="utf-8")
    say(f"removed {drop}, one of {len(listed)} that each follow from the others")
    return True


def repair_loop(part):
    for attempt in range(1, 6):
        say(f"attempt {attempt}: kalvec build --json {part.name}")
        code, out = kalvec("build", part, "--json")
        snapshot = json.loads(out)
        errors = [d for d in snapshot["diagnostics"] if d["severity"] == "error"]
        if code == 0:
            say("every check passes")
            return True
        problem = errors[0]
        say(f"line {problem['line']} [{problem['code']}] {problem['message']}")
        if problem["hint"]:
            say(f"hint: {problem['hint']}")
        if problem["code"] == "selector":
            fixed = repair_selector(part, problem)
        elif problem["code"] == "check":
            fixed = repair_check(part, snapshot)
        elif problem["code"] == "over":
            fixed = repair_repeat(part, problem)
        else:
            say("no repair strategy for this diagnostic")
            fixed = False
        if not fixed:
            return False
    return False


def finish(part):
    code, out = kalvec("fmt", part, "--check")
    if code != 0:
        kalvec("fmt", part, "--write")
        say("formatted the source canonically")
    kalvec("render", part, part.with_suffix(".png"), "--labels")
    kalvec("export", part, part.with_suffix(".step"))
    code, out = kalvec("describe", part)
    say(out.strip())
    say(f"final source:\n{part.read_text(encoding='utf-8')}")


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    for name, draft in (("plate.kd", PLATE_DRAFT), ("tab.kd", TAB_DRAFT)):
        part = WORK / name
        part.write_text(draft, encoding="utf-8")
        say(f"wrote a first draft to {part}")
        if not repair_loop(part):
            return 1
        finish(part)
    return 0


if __name__ == "__main__":
    sys.exit(main())
