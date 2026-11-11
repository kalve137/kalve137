# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(nlohmann_json CONFIG REQUIRED)

add_library(kalve137_core STATIC
    document.cpp
    document.h
    expression.cpp
    expression.h
    feature.cpp
    feature.h
    history.cpp
    history.h
    naming.cpp
    naming.h
    project.cpp
    project.h
    recompute.cpp
    recompute.h
    serialization.cpp
    serialization.h
    shape_store.cpp
    shape_store.h
    undo_stack.cpp
    undo_stack.h
)
target_include_directories(kalve137_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_core
    PUBLIC kalve137_geometry kalve137_sketch
    PRIVATE kalve137_log nlohmann_json::nlohmann_json TKTopAlgo TKBRep
)
kalve137_set_warnings(kalve137_core)
