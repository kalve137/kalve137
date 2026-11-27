# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_camera STATIC
    camera.cpp
    camera.h
    gravity.cpp
    gravity.h
    math.cpp
    math.h
    navigation.h
    navigator.cpp
    navigator.h
    transition.cpp
    transition.h
    views.cpp
    views.h
)
target_include_directories(kalve137_camera PUBLIC ${PROJECT_SOURCE_DIR}/src)
kalve137_set_warnings(kalve137_camera)
kalve137_forbid(kalve137_camera QT OCCT MODULES app core geometry pick render sketch snapshot viewer)
