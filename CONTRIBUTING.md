# Contributing to Kalve 137

Report security issues privately as described in [SECURITY.md](SECURITY.md).

## Build and test

You need Visual Studio 2022 or newer with the C++ workload, Qt 6.12 or newer (MSVC 64-bit kit) with the Qt Shader Tools add-on, vcpkg and LLVM, with `VCPKG_ROOT` and `QT_ROOT_DIR` set.

```powershell
. .\scripts\dev-shell.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The first configure builds OpenCascade and takes 30 to 60 minutes. UI tests drive a hidden window and save screenshots to `build\<preset>\ui-shots`. `.\scripts\package.ps1` builds and checks the release zip.

## Code

- `.\scripts\format.ps1` and `.\scripts\lint.ps1` must pass. `git config core.hooksPath .githooks` checks formatting and licensing on every commit.
- Every file has an SPDX header or an entry in `REUSE.toml`, and `reuse lint` passes. The license is GPL-3.0-or-later.
- No comments in `src` and `tests` apart from the SPDX header and `NOLINT` with a reason. Names explain the code.
- American English everywhere.
- OS-specific code lives only in `src/platform`. `core` and `geometry` use no Qt.
- Vendored code goes verbatim into `third_party/<name>/` with its license and a README naming the upstream commit. Shipped libraries are listed in `THIRD-PARTY-NOTICES.md`.

## Pull requests

Branch from `dev` with a type prefix such as `feat/`, `fix/` or `docs/`, which sets the label. Open the pull request against `dev`.

The title is a capitalized imperative within 65 characters and without a trailing period, as in `Add taper to revolve`. Title and description become the squash commit, so fill in **Summary** and **Test plan**.

Run the tests that cover your change, add a line under `[Unreleased]` in `CHANGELOG.md` for anything users notice and check that `format.ps1`, `lint.ps1` and `reuse lint` pass.

Pull requests squash-merge after quick checks. Every merge into `dev` runs the full Windows build and `main` only takes `dev` commits that passed it.

## Versions

Versions follow [Semantic Versioning](https://semver.org/), with the `.k137` format and the command line as the public interface. Older files always keep loading. Release tags are `vX.Y.Z`.
