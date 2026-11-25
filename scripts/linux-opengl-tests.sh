#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

# Run the viewport UI tests of ctest's Viewport.* on OpenGL with Mesa llvmpipe under Xvfb, after a
# linux-release build. Tests in EXCLUDED don't run here and are printed with their reason. Fails
# when a test fails, when no test ran, when the viewport found no OpenGL device or when a
# reference color test skipped.

set -euo pipefail

sudo=""
if [ "$(id -u)" -ne 0 ]; then
    sudo="sudo"
fi

export DEBIAN_FRONTEND=noninteractive
$sudo apt-get install -y --no-install-recommends xvfb xauth mesa-utils libgl1-mesa-dri libglx-mesa0 > /dev/null

export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export QT_QPA_PLATFORM=xcb
export QT_XCB_GL_INTEGRATION=xcb_glx

build=build/linux-release

cat > "$build/opengl-tests.py" <<'EOF'
import json
import os
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET

EXCLUDED = {
    "MarkerPairTest.ConstructionAndOriginPointsDrawOverTheBodyLikeTheOldView":
        "old view differs on Linux, viewport same on both",
    "BadgePairTest.EveryBadgeAndLabelKeepsTheOldViewsSizeCornerTextAndColor":
        "old view differs on Linux, viewport same on both",
    "ScreenTest.FloorFitsTheWindowAndBothToolbars":
        "sketch bar drops its labels at full width with the Linux fonts",
    "SelectTest.WindowOverThousandsOfFacesStaysFast":
        "window selection takes 1165 ms on the CPU rasterizer, budget 1000 ms",
    "FeatureTest.IdleHoverAndSelectStayFast":
        "worst viewport select takes 270 to 300 ms on the CPU rasterizer, budget 150 ms",
}

build = pathlib.Path(sys.argv[1]).resolve()
plan = json.loads((build / "opengl-tests.json").read_text())
results = build / "opengl-results"
results.mkdir(exist_ok=True)
for old in results.glob("*.xml"):
    old.unlink()

for name, reason in EXCLUDED.items():
    print(f"excluded {name}: {reason}")

listed = set()
status = 0
for test in plan["tests"]:
    properties = {p["name"]: p["value"] for p in test.get("properties", [])}
    cwd = properties.get("WORKING_DIRECTORY", str(build / "tests"))
    env = dict(os.environ)
    for entry in properties.get("ENVIRONMENT", []):
        key, _, value = entry.partition("=")
        env[key] = value
    command = list(test["command"])
    index = next(i for i, arg in enumerate(command) if arg.startswith("--gtest_filter="))
    listing = subprocess.run(command + ["--gtest_list_tests"], env=env, cwd=cwd, capture_output=True,
                             text=True, check=True)
    suite = ""
    for line in listing.stdout.splitlines():
        if not line.startswith(" "):
            suite = line.strip()
        elif line.strip():
            listed.add(suite + line.split()[0])
    pattern = command[index].partition("=")[2]
    command[index] += (":" if "-" in pattern else "-") + ":".join(EXCLUDED)
    command.append(f"--gtest_output=xml:{results / (test['name'] + '.xml')}")
    print(f"Running {test['name']}", flush=True)
    if subprocess.run(command, env=env, cwd=cwd).returncode != 0:
        print(f"{test['name']} failed", file=sys.stderr)
        status = 1

for name in sorted(set(EXCLUDED) - listed):
    print(f"{name} is excluded but no Viewport test lists it", file=sys.stderr)
    status = 1

ran = []
skipped = []
for path in sorted(results.glob("*.xml")):
    for case in ET.parse(path).getroot().iter("testcase"):
        if case.get("status") != "run":
            continue
        name = f"{case.get('classname')}.{case.get('name')}"
        if case.get("result") == "skipped":
            node = case.find("skipped")
            message = node.get("message", "") if node is not None else ""
            skipped.append((name, " ".join(message.split())))
        else:
            ran.append(name)

print(f"{len(ran)} ran, {len(skipped)} skipped, {len(EXCLUDED)} excluded")
for name, message in skipped:
    print(f"skipped {name}: {message}")
if not ran:
    print("No viewport test ran", file=sys.stderr)
    status = 1
if any("no usable" in message for _, message in skipped):
    print("The viewport found no OpenGL device", file=sys.stderr)
    status = 1
if any("OpenGL 3.2" in message for _, message in skipped):
    print("A reference color test skipped although llvmpipe has OpenGL 4.5", file=sys.stderr)
    status = 1
sys.exit(status)
EOF

xvfb-run -a -s "-screen 0 1920x1080x24" bash -euo pipefail -c '
    glxinfo -B | grep -E "OpenGL (renderer|core profile version) string"
    ctest --preset linux-release -R "^Viewport\." --show-only=json-v1 > "$1/opengl-tests.json"
    python3 "$1/opengl-tests.py" "$1"
' bash "$build"
