# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_render_rhi STATIC
    glyph_atlas.cpp
    glyph_atlas.h
    rhi_annotations.cpp
    rhi_backend.cpp
    rhi_backend.h
    rhi_backend_detail.h
    rhi_datums.cpp
    rhi_ids.cpp
    rhi_pipelines.cpp
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
        Qt6::Svg
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
        shaders/halo.vert
        shaders/halo.frag
        shaders/dash.vert
        shaders/dash.frag
        shaders/layer.vert
        shaders/layer.frag
        shaders/grid_line.vert
        shaders/grid_fill.vert
        shaders/grid.frag
        shaders/grid_depth.frag
        shaders/datum_line.vert
        shaders/datum.frag
        shaders/depth_reset.frag
        shaders/marker.vert
        shaders/marker.frag
        shaders/mask.frag
        shaders/post.vert
        shaders/post.frag
        shaders/id_face.vert
        shaders/id_edge.vert
        shaders/id.frag
        shaders/id_datum_fill.vert
        shaders/id_datum_line.vert
        shaders/id_datum.frag
        shaders/id_depth_reset.frag
        shaders/id_marker.vert
        shaders/annotation.vert
        shaders/annotation.frag
)
kalve137_set_warnings(kalve137_render_rhi)
kalve137_forbid(kalve137_render_rhi OCCT MODULES app core geometry sketch viewer)
