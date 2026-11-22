# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_render INTERFACE)
target_sources(kalve137_render INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/src
    FILES
        graphics_api.h
        positions.h
        style.h
)
target_link_libraries(kalve137_render INTERFACE kalve137_snapshot)
kalve137_forbid(kalve137_render QT OCCT MODULES app core geometry render/rhi sketch viewer)
