# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_snapshot INTERFACE)
target_sources(kalve137_snapshot INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/src
    FILES
        body.h
        color.h
        mesh.h
)
kalve137_forbid(kalve137_snapshot QT OCCT MODULES app core geometry render sketch viewer)
