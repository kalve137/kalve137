# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_lang STATIC
    ast.h
    check.cpp
    check.h
    edit.cpp
    edit.h
    format.cpp
    format.h
    parse.cpp
    parse.h
    reference.cpp
    reference.h
)
target_include_directories(kalve137_lang PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(kalve137_lang PUBLIC cxx_std_20)
