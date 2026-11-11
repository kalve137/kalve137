# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_platform OBJECT platform.h)

if(WIN32)
    target_sources(kalve137_platform PRIVATE platform_win.cpp)
    target_link_libraries(kalve137_platform PUBLIC dbghelp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(kalve137_platform PRIVATE platform_linux.cpp)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_include_directories(kalve137_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
kalve137_set_warnings(kalve137_platform)
