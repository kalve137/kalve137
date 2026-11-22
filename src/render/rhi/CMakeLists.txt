# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_render_rhi STATIC
    rhi_backend.cpp
    rhi_backend.h
    rhi_view.cpp
    rhi_view.h
)
target_include_directories(kalve137_render_rhi PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(kalve137_render_rhi
    PUBLIC
        kalve137_camera
        kalve137_render
        Qt6::Widgets
    PRIVATE
        kalve137_log
        Qt6::GuiPrivate
)
qt_add_shaders(kalve137_render_rhi "kalve137_body_shaders"
    PREFIX "/kalve137/shaders"
    BASE shaders
    # The palette lookup needs texelFetch, which GLSL ES 100 and GLSL 120 lack.
    GLSL "300 es,150"
    HLSL 50
    MSL 12
    FILES
        shaders/body.vert
        shaders/body.frag
        shaders/edge.vert
        shaders/edge.frag
)
kalve137_set_warnings(kalve137_render_rhi)
kalve137_forbid(kalve137_render_rhi OCCT MODULES app core geometry sketch viewer)
