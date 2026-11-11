# occt_qt

OpenCascade glue for Qt OpenGL widgets.

- Upstream: [gkv311/occt-samples-qopenglwidget](https://github.com/gkv311/occt-samples-qopenglwidget), `occt-qt-tools` at commit `72e32d3a8398229702030ffc22b9f847ba7ad5d0`
- Files: `OcctGlTools` and `OcctQtTools`, verbatim apart from the patch below
- License: MIT, `LICENSE.txt` from upstream
- Local patch: `Graphic3d_Vec2i` and `Graphic3d_Vec2d` are replaced by `NCollection_Vec2`, since OCCT 8 deprecates `Graphic3d_Vec2.hxx` and no longer pulls it in transitively

`CMakeLists.txt` is ours, MIT like the library, and builds the files as a static library.
