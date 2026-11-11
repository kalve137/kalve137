# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_geometry STATIC
    exchange.cpp
    exchange.h
    face_colors.cpp
    face_colors.h
    face_names.cpp
    face_names.h
    measure.cpp
    measure.h
    modeling.cpp
    modeling.h
    shapes.cpp
    shapes.h
    topology.cpp
    topology.h
)
target_include_directories(kalve137_geometry PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_geometry
    PUBLIC
        TKBRep
        TKMath
        TKernel
    PRIVATE
        kalve137_log
        TKBO
        TKDESTEP
        TKDESTL
        TKFillet
        TKG3d
        TKLCAF
        TKMesh
        TKOffset
        TKPrim
        TKShHealing
        TKTopAlgo
        TKXCAF
        TKXSBase
)
kalve137_set_warnings(kalve137_geometry)
