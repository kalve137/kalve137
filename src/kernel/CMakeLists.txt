# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_kernel STATIC
    shapes.cpp
    shapes.h
)
target_include_directories(kalve137_kernel PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(kalve137_kernel PUBLIC cxx_std_20)
target_link_libraries(kalve137_kernel
    PRIVATE
        kalve137_geometry
        TKBRep TKMath TKernel TKBO TKFillet TKG2d TKG3d TKGeomAlgo TKGeomBase TKMesh TKPrim TKTopAlgo
)
