# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Threads REQUIRED)

add_library(kalve137_render STATIC
    chunk_set.cpp
    chunk_set.h
    cpu_backend.cpp
    cpu_backend.h
    draw_list.cpp
    draw_list.h
    graphics_api.h
    positions.h
    style.h
    thread_pool.cpp
    thread_pool.h
)
target_include_directories(kalve137_render PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_render PUBLIC kalve137_camera kalve137_snapshot Threads::Threads)
# The CPU backend must round the same on every compiler, so vertices shared by two faces never crack apart.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(kalve137_render PRIVATE /clang:-ffp-contract=off)
    else()
        target_compile_options(kalve137_render PRIVATE -ffp-contract=off)
    endif()
endif()
kalve137_set_warnings(kalve137_render)
kalve137_forbid(kalve137_render QT OCCT MODULES app core geometry render/rhi sketch viewer)
