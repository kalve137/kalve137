# Changelog

Notable changes per release. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow [Semantic Versioning](https://semver.org/) as described in [CONTRIBUTING.md](CONTRIBUTING.md#versions).

## [Unreleased]

### Added

- `--import-batch` command line mode that measures STEP import, save, reopen and export over a folder
- Right Orbits, Ctrl Pans mouse scheme with marking menu on middle click
- Dimension labels move by dragging them, as one undo step
- Copy Details in About for bug reports
- Help, Open Sample opens dodecahedron as an untitled copy
- Light theme. Follows system setting by default, Preferences switches it live
- Concentric sketch constraint joins the centers of two circles or arcs
- Dimension and handle value boxes offer the last five typed values from a dropdown or Alt+Down
- Lines and box selections drag as a whole in sketches
- Move takes typed X and Y offsets for selected sketch geometry
- Timeline right click offers Edit Profile Sketch and Find in Browser
- Hovering a timeline feature highlights bodies it shaped
- Slot, Center Point Slot and Overall Slot tools in sketches
- Offset tool copies sketch curves at a distance and keeps the copy linked to its source
- Project tool brings model edges, vertices and flat faces into a sketch, where they follow the model
- Parameters dialog under Edit names values such as `w = 40 mm`
- Value fields and sketch dimensions take parameter names and expressions such as `w * 2`
- Concentric constraints show a badge in sketches

### Changed

- Finishing a sketch returns view to where it was before
- Look at sketch plane preference has a clearer name
- Editing a dimension opens value box in place of its label
- About shows version and license, with graphics details in Copy Details
- Close prompts say Don't Save instead of Discard
- Repeat runs last command available in current mode
- Window and crossing selection over large imports takes about a second instead of up to several minutes
- Large STEP imports show up sooner since faces are meshed in parallel
- View cube is a shaded rhombicuboctahedron with larger edge and corner targets
- View cube hover reads clearly in both themes, labels included
- View cube faces, edges and corners stand apart in both themes, with larger labels and solid gold on hover
- Toolbars switch to smaller icons without labels on narrow windows, so every tool and Finish Sketch stay in reach
- Viewport lines and grid labels follow display scaling
- Designs save in a newer file format that 0.1.0 can't open. Designs from 0.1.0 open as before

### Fixed

- Closing Recover Design prompt keeps autosave
- Default buttons are highlighted, including Save Zip and Restore
- Sketch lines on grid plane no longer flicker against grid lines
- Body edges and their selection highlight on grid plane no longer look dashed
- Cut from a face sketch goes into body
- Line tool angle box stays clear of line
- Status bar hint returns after a menu closes
- Dialogs show only a close button
- Third-Party Notices open wider
- File dialogs start in last used folder when no project is open
- Dialogs and first window fit on small screens
- Long selections in feature panels scroll instead of pushing OK out of view
- Report a Problem notes missing crash report or unsaved design instead of disabling its checkbox
- Dimension lines keep their contrast over light faces
- Marking menu list sits lower and skips disabled and duplicate recent commands
- Clicking a dimension label edits it without moving mouse first

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
