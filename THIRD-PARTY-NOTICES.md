# Third-party notices

Kalve 137 is free software under GPL-3.0-or-later. It is built on the components below. Release package carries full license text of each in its `notices` folder.

| Component | Version | License | Role |
| --- | --- | --- | --- |
| [Qt](https://www.qt.io/download-open-source) | 6.12.0 | LGPL-3.0-only | UI toolkit |
| [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT) | 8.0.1 | LGPL-2.1-only with the OCCT exception | Modeling kernel and 3D view |
| [PlaneGCS](third_party/planegcs/README.md) | 1.1.4 | LGPL-2.0-or-later | Sketch constraint solver |
| [occt-qt-tools](third_party/occt_qt/README.md) | 72e32d3 | MIT | OpenGL glue between the kernel and Qt |
| [tinybvh](third_party/tinybvh/README.md) | 769343f | MIT | Ray casting for box selection |
| [Boost](https://www.boost.org) | 1.92.0 | BSL-1.0 | Used by the solver |
| [Eigen](https://eigen.tuxfamily.org) | 5.0.1 | MPL-2.0 | Used by the solver |
| [spdlog](https://github.com/gabime/spdlog) | 1.17.0 | MIT | Logging |
| [{fmt}](https://github.com/fmtlib/fmt) | 12.2.0 | MIT | Text formatting |
| [JSON for Modern C++](https://github.com/nlohmann/json) | 3.12.0 | MIT | Design files |
| [miniz](https://github.com/richgel999/miniz) | 3.1.2 | MIT | Compression |
| [FreeType](https://freetype.org) | 2.15.0 | FTL | Text in the 3D view |
| [libpng](http://www.libpng.org/pub/png/libpng.html) | 1.6.59 | libpng-2.0 | Images |
| [zlib](https://zlib.net) | 1.3.2 | Zlib | Compression |
| [bzip2](https://sourceware.org/bzip2/) | 1.0.8 | bzip2-1.0.6 | Compression |
| [Brotli](https://github.com/google/brotli) | 1.2.1 | MIT | Compression |
| [Roboto](third_party/roboto/README.md) | 3.016 | OFL-1.1 | UI font |

## LGPL libraries

Qt, Open CASCADE Technology and PlaneGCS are linked dynamically. You may replace their DLLs in the install folder with your own builds of a compatible version. Their source code is available from the links above, and the PlaneGCS sources used here are in this repository under `third_party/planegcs`.

## Code adapted from Open CASCADE Technology

The new viewport's camera moves exactly like the 3D view built on Open CASCADE Technology 8.0.1, so parts of the files below are translated from its source. That code stays under LGPL-2.1-only with the Open CASCADE exception 1.0 and is carried under GPL-3.0-or-later as section 3 of the LGPL allows. Each file names both in its SPDX header.

| File | Follows |
| --- | --- |
| `src/camera/camera.h`, `src/camera/camera.cpp` | `Graphic3d_Camera` (defaults, setters, view dimensions, FitMinMax, ZFitAll) and the pixel conversion of `V3d_View` |
| `src/camera/gravity.cpp` | `V3d_View::GravityPoint` |
| `src/camera/navigator.cpp` | `AIS_ViewController` (orbit, pan, zoom and their constants) |
| `src/camera/transition.cpp` | The camera interpolation in `Graphic3d_Camera.cxx` and `gp_Quaternion::SetMatrix` |
| `src/camera/views.cpp` | `V3d_View::SetProj` |
| `src/viewer/occt_view_widget_camera.cpp` | `V3d_View::GravityPoint`, `V3d_View::ConvertWithProj` and `V3d_View::Convert` |
