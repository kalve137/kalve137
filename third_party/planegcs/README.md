# PlaneGCS

The 2D geometric constraint solver from FreeCAD's Sketcher.

- Upstream: [FreeCAD/FreeCAD](https://github.com/FreeCAD/FreeCAD), tag 1.1.4 (commit `4fd3bf320d9566a27e60069fc8387448aaa3a094`), `src/Mod/Sketcher/App/planegcs`
- Files: everything under `Sketcher/App/planegcs`, verbatim
- License: LGPL-2.0-or-later as stated in each source file. `LICENSE` holds the LGPL-2.1 text that FreeCAD ships.

Ours, under LGPL-2.1-or-later:
- `shim/` and `Sketcher/SketcherGlobal.h` are minimal stand-ins for the FreeCAD headers the sources include. They compile, export symbols and drop solver diagnostics, nothing more.
- `CMakeLists.txt` builds a shared library, so the rest of Kalve 137 links to it dynamically.
