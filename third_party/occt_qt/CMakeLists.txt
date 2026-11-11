# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: MIT

add_library(occt_qt STATIC
    OcctGlTools.h
    OcctGlTools.cpp
    OcctQtTools.h
    OcctQtTools.cpp
)
target_include_directories(occt_qt SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(occt_qt
    PUBLIC
        Qt6::Gui
        TKV3d
        TKService
        TKOpenGl
    PRIVATE
        OpenGL::GL
)
if(MSVC)
    target_compile_options(occt_qt PRIVATE /utf-8)
endif()
