# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_solve STATIC
    solve.cpp
    solve.h
)
target_include_directories(kalve137_solve PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(kalve137_solve PUBLIC cxx_std_20)
target_link_libraries(kalve137_solve PRIVATE planegcs)
