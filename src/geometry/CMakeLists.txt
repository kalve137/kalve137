# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_geometry STATIC
    body_mesh.cpp
    body_mesh.h
    exchange.cpp
    exchange.h
    face_colors.cpp
    face_colors.h
    face_names.cpp
    face_names.h
    measure.cpp
    measure.h
    meshing.cpp
    meshing.h
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
        kalve137_snapshot
        TKBRep
        TKMath
        TKernel
    PRIVATE
        kalve137_log
        kalve137_pick
        TKBO
        TKDESTEP
        TKDESTL
        TKFillet
        TKG3d
        TKGeomBase
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
