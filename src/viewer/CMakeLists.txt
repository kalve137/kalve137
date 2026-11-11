# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_viewer STATIC
    box_band.h
    box_band.cpp
    camera_guard.h
    camera_guard.cpp
    grid.h
    grid.cpp
    navigation.h
    occt_view_widget.h
    occt_view_widget.cpp
    occt_view_widget_box.cpp
    occt_view_widget_grid.cpp
    style.h
    view_turn.h
    view_turn.cpp
)
target_include_directories(kalve137_viewer PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_viewer
    PUBLIC
        Qt6::OpenGLWidgets
        Qt6::Widgets
        TKV3d
        TKBRep
    PRIVATE
        kalve137_log
        occt_qt
        TKOpenGl
        TKService
)
kalve137_set_warnings(kalve137_viewer)
