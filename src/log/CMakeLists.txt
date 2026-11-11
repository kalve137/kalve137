# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(miniz CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(kalve137_log STATIC
    log.cpp
    log.h
    occt_printer.cpp
    occt_printer.h
    report_zip.cpp
    report_zip.h
)
target_include_directories(kalve137_log PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_log
    PUBLIC spdlog::spdlog
    PRIVATE miniz::miniz TKernel
)
target_compile_definitions(kalve137_log PUBLIC
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_TRACE,SPDLOG_LEVEL_DEBUG>
)
kalve137_set_warnings(kalve137_log)
