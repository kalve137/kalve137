#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

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
import concurrent.futures
import json
import os
import pathlib
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

DEFAULT_TIMEOUT = 180
DRAWABLE = "failed to create drawable"
GLX_HEADER = "TKOpenGl.WinSystem | Type: Error | ID: 0 | Severity: High | Message:"
GLX_FAILED = "glXMakeCurrent() has failed!"

EXCLUDED = {
    "MarkerPairTest.ConstructionAndOriginPointsDrawOverTheBodyLikeTheOldView":
        "old view differs on Linux, viewport same on both",
    "SelectTest.WindowOverThousandsOfFacesStaysFast":
        "window selection takes 1165 ms on the CPU rasterizer, budget 1000 ms",
    "FeatureTest.IdleHoverAndSelectStayFast":
        "worst viewport select takes 270 to 300 ms on the CPU rasterizer, budget 150 ms",
}

build = pathlib.Path(sys.argv[1]).resolve()
jobs = max(1, int(sys.argv[2]))
plan = json.loads((build / "opengl-tests.json").read_text())
results = build / "opengl-results"
results.mkdir(exist_ok=True)
for old in results.glob("*.xml"):
    old.unlink()
printing = threading.Lock()


def run(test):
    process = subprocess.Popen(test["command"], env=test["env"], cwd=test["cwd"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors="replace")
    expired = threading.Event()

    def expire():
        expired.set()
        process.kill()

    started = time.monotonic()
    timer = threading.Timer(test["timeout"], expire)
    timer.start()
    output = []
    drawables = 0
    glx = 0
    held = None
    for line in process.stdout:
        if held is not None:
            if line.strip() == GLX_FAILED:
                glx += 1
                held = None
                continue
            output.append(held)
            held = None
        if line.strip() == DRAWABLE:
            drawables += 1
        elif line.rstrip() == GLX_HEADER:
            held = line
        else:
            output.append(line)
    if held is not None:
        output.append(held)
    returncode = process.wait()
    timer.cancel()
    name = test["name"]
    with printing:
        sys.stdout.write("".join(output))
        took = time.monotonic() - started
        print(f"{name}: {took:.1f} s, hid {drawables} '{DRAWABLE}' and {glx} '{GLX_FAILED}' lines", flush=True)
        if expired.is_set():
            print(f"{name} timed out after {test['timeout']:.0f} s", file=sys.stderr, flush=True)
        elif returncode != 0:
            print(f"{name} failed", file=sys.stderr, flush=True)
    return returncode == 0 and not expired.is_set(), drawables, glx


for name, reason in EXCLUDED.items():
    print(f"excluded {name}: {reason}")

listed = set()
parallel = []
serial = []
for test in plan["tests"]:
    properties = {p["name"]: p["value"] for p in test.get("properties", [])}
    command = list(test["command"])
    gtest_name = next(arg for arg in command if arg.startswith("--gtest_filter=")).partition("=")[2]
    listed.add(gtest_name)
    if gtest_name in EXCLUDED or properties.get("DISABLED"):
        continue
    env = dict(os.environ)
    for entry in properties.get("ENVIRONMENT", []):
        key, _, value = entry.partition("=")
        env[key] = value
    command.append(f"--gtest_output=xml:{results / (test['name'] + '.xml')}")
    job = {
        "name": test["name"],
        "command": command,
        "env": env,
        "cwd": properties.get("WORKING_DIRECTORY", str(build / "tests")),
        "timeout": float(properties.get("TIMEOUT", DEFAULT_TIMEOUT)),
    }
    alone = properties.get("RUN_SERIAL") or properties.get("RESOURCE_LOCK")
    (serial if alone else parallel).append(job)

print(f"Running {len(parallel)} tests on {jobs} jobs, then {len(serial)} alone", flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    outcomes = list(pool.map(run, parallel))
outcomes += [run(test) for test in serial]
status = 0 if all(passed for passed, _, _ in outcomes) else 1
drawables = sum(count for _, count, _ in outcomes)
glx = sum(count for _, _, count in outcomes)

notes = [f"Hid {drawables} '{DRAWABLE}' and {glx} '{GLX_FAILED}' lines from the old view's hidden window"]
(build / "opengl-report.json").write_text(json.dumps({"excluded": EXCLUDED, "notes": notes}, indent=2))

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
    python3 "$1/opengl-tests.py" "$1" "$(nproc)"
' bash "$build"
