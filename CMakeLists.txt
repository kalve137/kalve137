# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

project(kalve137
    VERSION 0.1.0
    DESCRIPTION "Kalve 137 parametric CAD"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Kalve137Warnings)
include(Kalve137Hardening)
include(Kalve137Deploy)

find_package(OpenCASCADE CONFIG REQUIRED)
if(WIN32)
    foreach(definition HAVE_FREETYPE HAVE_OPENGL_EXT NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
        add_compile_definitions($<$<CONFIG:RelWithDebInfo>:${definition}>)
    endforeach()
endif()
find_package(OpenGL REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg)
qt_standard_project_setup()

add_subdirectory(third_party/occt_qt)
add_subdirectory(third_party/planegcs)
add_subdirectory(src/platform)
add_subdirectory(src/log)
add_subdirectory(src/geometry)
add_subdirectory(src/sketch)
add_subdirectory(src/core)
add_subdirectory(src/viewer)
add_subdirectory(src/app)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME kalve137)
set(CPACK_PACKAGE_VENDOR "Danius Kalvaitis")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" package_system)
set(CPACK_PACKAGE_FILE_NAME "kalve137-${PROJECT_VERSION}-${package_system}-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
