# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_look STATIC
    image.cpp
    image.h
    raster.cpp
    raster.h
)
target_link_libraries(kalve137_look PUBLIC kalve137_model Qt6::Gui)
