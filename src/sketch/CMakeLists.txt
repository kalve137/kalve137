# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_sketch STATIC
    build.cpp
    build.h
    dimension.cpp
    dimension.h
    dimension_layout.cpp
    dimension_layout.h
    draft.cpp
    draft.h
    edit.cpp
    edit.h
    profiles.cpp
    profiles.h
    select.cpp
    select.h
    sketch.cpp
    sketch.h
    snapshot.cpp
    snapshot.h
    solver.cpp
    solver.h
)
target_include_directories(kalve137_sketch PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_sketch
    PUBLIC kalve137_snapshot TKBRep TKMath TKernel
    PRIVATE kalve137_log planegcs TKBO TKTopAlgo TKMesh TKG3d
)
kalve137_set_warnings(kalve137_sketch)
