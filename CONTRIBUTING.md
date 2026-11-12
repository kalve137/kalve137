# Contributing to Kalve 137

Report security issues privately as described in [SECURITY.md](SECURITY.md), not in public issues.

## Build on Windows

Prerequisites:

- Visual Studio 2022 or newer with the "Desktop development with C++" workload
- Qt 6.8 or newer, MSVC 64-bit kit (for example `python -m aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -O C:\Qt`)
- vcpkg cloned and bootstrapped (for example in `C:\Tools\vcpkg`)
- Environment variables `VCPKG_ROOT` (vcpkg checkout) and `QT_ROOT_DIR` (Qt kit, for example `C:\Qt\6.10.2\msvc2022_64`)

From repo root, enter build environment first. It sets up MSVC and picks our vcpkg plus Visual Studio's CMake and Ninja:

```powershell
. .\scripts\dev-shell.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows-debug\kalve137.exe
```

First configure builds OpenCascade through vcpkg and takes 30 to 60 minutes. Later runs reuse vcpkg binary cache.

## Tests

`ctest` runs two executables. `kalve137_tests` covers modules without UI. `kalve137_uitests` drives a hidden main window through `tests/ui/ui_driver.h`, so it opens nothing on screen and sends no input to other windows. Each UI test saves numbered screenshots to `build\<preset>\ui-shots\<Test>\` for review.

```powershell
ctest --preset windows-release
.\build\windows-release\kalve137_uitests.exe --gtest_filter=DimensionTest.*
```

`assets/samples/dodecahedron.k137` is generated from the recipe in `tests/core/sample_test.cpp`. After changing recipe or file format, rewrite it with `$env:KALVE137_UPDATE_SAMPLES = 1` and run `Sample.*` once. `HeroTest` records the README animation frames when `KALVE137_HERO_FRAMES` names a folder and skips otherwise.

## Package

```powershell
.\scripts\package.ps1
```

This builds `kalve137`, runs CPack into `build\windows-release\package\kalve137-<version>-windows-x64.zip`, unpacks zip to a temporary folder and checks it. The check fails on a missing DLL, plugin or notice, on debug symbols or installers in the zip and on a `qt.conf` with an absolute path. It then runs `kalve137.exe --smoke` on the sample with `PATH` reduced to Windows folders, so a DLL that is only found on the build machine fails the run. `-SkipBuild` reuses the last build.

Libraries shipped in the zip are listed in `THIRD-PARTY-NOTICES.md` and their license texts are installed to `bin\notices`. Update both when adding a dependency.

Linux presets come later. The code is kept portable now, see below.

## Workflow

`main` is always releasable and is the only long-lived branch. Work happens on short-lived branches named after their kind:

| Prefix | For | Label |
| --- | --- | --- |
| `feature/` | New behavior | `enhancement` |
| `fix/` | Bug fixes | `bug` |
| `refactor/` | Code changes that keep behavior | `refactor` |
| `docs/` | Documentation only | `documentation` |
| `ci/` | Workflows, scripts and build setup | `ci` |

Pull requests get labeled automatically from `.github/labeler.yml`: a type label from the branch prefix and `area:` labels from the paths they touch.

Open a pull request against `main`. It merges once the `Version` and `Windows` checks pass and lands as a single squashed commit, so the pull request title becomes the commit subject. Write it like a commit subject: imperative, capitalized, no trailing period and no type prefix, as in `Add taper to revolve`. Branches are deleted on merge.

Every user-visible change adds a line under `## [Unreleased]` in `CHANGELOG.md`, in the matching Keep a Changelog section such as `Added`, `Changed` or `Fixed`.

## Versioning and releases

Versions follow [Semantic Versioning](https://semver.org/). The public interface is the `.k137` file format and the command line.

- Before 1.0, minor versions add features and may change the file format. Patch versions only fix bugs. Older files keep loading.
- From 1.0, a major version is needed when older files stop loading or a command line option changes meaning.
- Release tags are `vX.Y.Z`. Release candidates are `vX.Y.Z-rc.N`.

Version lives in one place, `project(kalve137 VERSION ...)` in `CMakeLists.txt`. `scripts/check-version.ps1` checks it against `CHANGELOG.md` and, with `-Tag`, against a tag. CI runs it first, so a mismatched tag fails in seconds instead of after the build.

```powershell
pwsh scripts/check-version.ps1
pwsh scripts/check-version.ps1 -Tag v0.2.0
```

To release:

1. Open a release pull request that sets the version in `CMakeLists.txt`, renames `## [Unreleased]` to `## [X.Y.Z] - YYYY-MM-DD`, adds a fresh `## [Unreleased]` above it and updates the compare links at the bottom.
2. After it merges, tag `main` with `git tag -a vX.Y.Z -m vX.Y.Z` and push the tag with `git push origin vX.Y.Z`.
3. CI checks the version, builds and drafts a GitHub release with the changelog section as notes and the zip attached.
4. Review the draft and publish it.

Tags are permanent. A wrong release is fixed with a new patch version, never by moving a tag. When an older minor version needs a fix after `main` has moved on, branch `release/X.Y` from `vX.Y.0`, fix it there through pull requests and tag the patch on that branch.

## Layout

| Directory | Contents | May depend on |
| --- | --- | --- |
| `src/platform` | Only place for OS-specific code | OS headers |
| `src/log` | Logging channels, crash ring and report zips | spdlog |
| `src/geometry` | Thin wrappers over OpenCascade modeling and STEP/STL export | OpenCascade, standard C++ |
| `src/sketch` | Sketch entities, constraints, PlaneGCS solving, profile detection | `third_party/planegcs`, OpenCascade |
| `src/core` | Design document, feature timeline, recompute, `.k137` format, local projects | `geometry`, `sketch`, standard C++ |
| `src/viewer` | OpenCascade 3D view inside a `QOpenGLWidget` | Qt, OpenCascade |
| `src/app` | `kalve137_app` library with UI grouped by area, plus `main.cpp` for the executable | everything above |
| `assets` | Icons, stylesheet and app branding, compiled into the executable | |
| `tests` | GoogleTest, one folder per `src` module plus `integration`, run through `ctest` | |
| `scripts` | Commands to run: dev shell, format, lint, package and symbolize | |
| `docs` | Principles, design rules and debugging | |
| `third_party` | Vendored code, kept verbatim with its license | |

`src/app` groups files by area: `shell` for the main window and its commands, `session` for the open design, preferences and autosave, `sketch`, `feature` for feature panels, `panels` for the docks, `view` for the scene and view controls, `theme` and `diagnostics`.

`src` is the only include root. Own headers are included with their module path in quotes, also from inside the same module:

```cpp
#include "core/document.h"
#include "app/theme/theme.h"
```

## Cross-platform rules

Windows is the supported platform today and Linux follows. Keep code from depending on Windows by accident:

- `core` and `geometry` use standard C++20 and OpenCascade only. No Qt, no OS headers.
- `windows.h` and `_WIN32` branches belong in `src/platform` only. Elsewhere use Qt: `QSettings`, `QStandardPaths`, `QFileDialog`, `QProcess`.
- Paths are `std::filesystem::path` in `core` and `QString` in the UI, never narrow `char*` paths.
- File names are lowercase and `#include` spelling matches the file exactly, since Linux filesystems are case-sensitive.
- Our libraries are static. Qt and OpenCascade stay dynamically linked on every platform to meet the LGPL. On Linux use the `x64-linux-dynamic` vcpkg triplet.
- New targets call `kalve137_set_warnings()`. MSVC builds with `/W4 /permissive-` and should stay warning-free.

## Formatting and linting

C++ under `src` and `tests` follows `.clang-format`. `.clang-tidy` adds bug-finding, performance and naming checks on `src` and `tests`. Code under `third_party/` is exempt from both.

Install [LLVM](https://github.com/llvm/llvm-project/releases) 18 or newer for `clang-format` and `clang-tidy`, for example with `winget install LLVM.LLVM`. The scripts look on `PATH` and then in `C:\Program Files\LLVM\bin`.

```powershell
.\scripts\format.ps1                 # format every file in place
.\scripts\format.ps1 -Check          # list unformatted files and exit 1
.\scripts\lint.ps1                   # clang-tidy on every source, needs a configured build
.\scripts\lint.ps1 -Files src\app\theme\theme.cpp
.\scripts\lint.ps1 -Fix              # apply the fixes clang-tidy offers, one file at a time
```

Set your editor to format C++ on save with `.clang-format`. Run `lint.ps1` from the dev shell after `cmake --preset windows-debug`, since it reads `build\windows-debug\compile_commands.json`. A full run takes a few minutes. Run it before opening a pull request. Every finding is an error, so fix it or suppress it with a reason, see Comments below.

A pre-commit hook rejects staged C++ that is not formatted and a tree that fails `reuse lint`, see Licensing below. Enable it once per clone:

```powershell
git config core.hooksPath .githooks
```

If the hook misbehaves, `git commit --no-verify` skips it.

Dot-sourcing `dev-shell.ps1` more than once in a session keeps growing `PATH` until commands fail with "input line is too long". Open a new terminal instead.

## Language

American English everywhere we write it: identifiers, user-facing strings, log messages, comments and docs. That means `color`, `center`, `canceled`, `labeled`, `normalize`, `behavior` and `license` as a noun.

Third-party names keep the library's spelling, so OpenCascade's `CentreOfMass()` stays as it is. Code under `third_party/` is never respelled.

To check before committing:

```powershell
rg -i 'colour|centre|cancelled|labelled|neighbour|behaviour|licence|[a-z]is(e|ed|ing|ation)\b' src tests docs --glob '!third_party/**'
```

The last pattern also matches correct words such as `raise`, `promise` and `exercise`, so read the hits rather than replacing blindly.

## Comments

Code documents itself. Sources under `src` and `tests` carry no comments apart from these:

- The SPDX header, see Licensing below.
- Tool directives with a reason after a colon: `// NOLINT(check-name): Why the finding does not apply.`
- The `// namespace` closers that clang-format maintains.

When something seems to need a comment, rename, extract a function or name a constant instead. A unit goes into the name, as in `kTimeoutMs`, and an invariant goes into a test. Vendored code under `third_party/` keeps its own comments.

## Licensing

Kalve 137 is licensed under GPL-3.0-or-later and follows the [REUSE](https://reuse.software/) specification, so every file states its copyright and license. License texts live in `LICENSES/`.

Every file that takes comments starts with this header, in the file's own comment style, followed by one blank line:

```cpp
// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later
```

Contributors who substantially change a file add their own `SPDX-FileCopyrightText` line below the existing ones. Files that can't or shouldn't carry a header, such as JSON, Markdown, images and vendored code, are covered by `REUSE.toml`. Add a matching entry there when adding such a file.

Install the checker with `pipx install "reuse[charset-normalizer]"` and run `reuse lint` from the repo root. It must pass before every commit.

## Third-party code

Vendored code goes under `third_party/<name>/` with its upstream license file and a `README.md` naming the upstream, the commit and any local patch. Vendored files stay verbatim and get their license through `REUSE.toml`. Code adapted from them keeps the original `SPDX-FileCopyrightText` line next to ours, with both licenses in `SPDX-License-Identifier`. Check the license is compatible with GPL-3.0-or-later before copying anything in.
