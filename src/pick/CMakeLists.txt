# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_pick STATIC
    ray_scene.cpp
    ray_scene.h
)
target_include_directories(kalve137_pick PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_pick PRIVATE tinybvh)
kalve137_set_warnings(kalve137_pick)
