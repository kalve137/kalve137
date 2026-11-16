# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(kalve_studio WIN32
    main.cpp
    rebuilder.cpp
    rebuilder.h
    view_widget.cpp
    view_widget.h
)
target_link_libraries(kalve_studio PRIVATE kalve137_rebuild kalve137_look Qt6::Widgets)
set_target_properties(kalve_studio PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
kalve137_copy_runtime_dlls(kalve_studio)
