# Changelog

Notable changes per release. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow [Semantic Versioning](https://semver.org/) as described in [CONTRIBUTING.md](CONTRIBUTING.md#versions).

## [Unreleased]

## [0.1.0] - 2026-11-11

First public release for 64-bit Windows.

### Added

- Projects as local folders and designs as `.k137` files with version history and autosave
- Sketches with lines, arcs, circles, rectangles, polygons, trim, fillet, mirror and construction geometry
- Geometric constraints and driving dimensions solved by PlaneGCS
- Dimension labels placed by a second click, with a live preview of the typed value
- Two-point dimensions that measure horizontally or vertically depending on where cursor leaves
- Sketches on construction planes and body faces that follow the face after upstream edits
- Extrude with distance, symmetric, To Object and All extents and taper angle
- Revolve, fillet with several radius sets, chamfer, shell, hole, pattern, move and press pull
- Face and edge picks stored as topological names so features rebuild after upstream edits
- Window and crossing selection that skips hidden edges
- Construction planes, axes and points
- Feature timeline with edit, suppress, roll back and undo
- STEP import and export with assemblies and colors and STL export
- Exported STEP files name Kalve 137 as the originating system
- Problem reports zipped from Help with log and crash data
- Third-party notices in Help, About and in release zip
- Dodecahedron sample design in `bin\samples`
- `--smoke` command line mode that checks an unpacked copy offscreen

[Unreleased]: https://github.com/kalve137/kalve137/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/kalve137/kalve137/releases/tag/v0.1.0
