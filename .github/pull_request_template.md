## What and why



## Checklist

- [ ] Tests cover the change and `ctest --preset windows-release` passes
- [ ] User-visible change has a line under `## [Unreleased]` in `CHANGELOG.md`
- [ ] `pwsh scripts/format.ps1 -Check`, `pwsh scripts/lint.ps1` and `reuse lint` pass
- [ ] Title reads as a commit subject, imperative with no trailing period
