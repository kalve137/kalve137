# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_app STATIC
    diagnostics/diagnostics.cpp
    diagnostics/diagnostics.h
    diagnostics/problem_report.cpp
    diagnostics/problem_report.h
    diagnostics/report_dialog.cpp
    diagnostics/report_dialog.h
    feature/distance_handle.cpp
    feature/distance_handle.h
    feature/feature_panel.cpp
    feature/feature_panel.h
    feature/feature_panel_body.cpp
    feature/feature_panel_construct.cpp
    feature/feature_panel_detail.h
    feature/feature_panel_form.cpp
    feature/feature_panel_selection.cpp
    feature/measure_panel.cpp
    feature/measure_panel.h
    feature/value_edit.cpp
    feature/value_edit.h
    panels/browser_panel.cpp
    panels/browser_panel.h
    panels/project_panel.cpp
    panels/project_panel.h
    panels/properties_dialog.cpp
    panels/properties_dialog.h
    panels/timeline_widget.cpp
    panels/timeline_widget.h
    session/autosave.cpp
    session/autosave.h
    session/design_session.cpp
    session/design_session.h
    session/paths.h
    session/preferences.cpp
    session/preferences.h
    session/preferences_dialog.cpp
    session/preferences_dialog.h
    shell/command_search.cpp
    shell/command_search.h
    shell/main_window.cpp
    shell/main_window.h
    shell/main_window_commands.cpp
    shell/main_window_files.cpp
    shell/main_window_help.cpp
    shell/main_window_modes.cpp
    shell/marking_menu.cpp
    shell/marking_menu.h
    shell/modal_scrim.cpp
    shell/modal_scrim.h
    shell/notices_dialog.cpp
    shell/notices_dialog.h
    shell/smoke.cpp
    shell/smoke.h
    shell/shortcuts_dialog.cpp
    shell/shortcuts_dialog.h
    shell/version_dialog.cpp
    shell/version_dialog.h
    sketch/constraint_badge.cpp
    sketch/constraint_badge.h
    sketch/dimension_label.cpp
    sketch/dimension_label.h
    sketch/sketch_editor.cpp
    sketch/sketch_editor.h
    sketch/sketch_editor_detail.h
    sketch/sketch_editor_draw.cpp
    sketch/sketch_editor_hit.cpp
    sketch/sketch_editor_shapes.cpp
    sketch/sketch_editor_tools.cpp
    sketch/sketch_inputs.cpp
    sketch/sketch_inputs.h
    sketch/sketch_palette.cpp
    sketch/sketch_palette.h
    theme/fonts.cpp
    theme/fonts.h
    theme/icons.cpp
    theme/icons.h
    theme/theme.cpp
    theme/theme.h
    theme/theme_tokens.cpp
    theme/theme_tokens.h
    view/grid_settings.cpp
    view/grid_settings.h
    view/scene.cpp
    view/scene.h
    view/scene_construction.cpp
    view/scene_detail.h
    view/view_controls.cpp
    view/view_controls.h
    view/view_cube_arrows.cpp
    view/view_cube_arrows.h
    view/view_overlay.cpp
    view/view_overlay.h
)

find_package(Git QUIET)
set(kalve137_git_hash "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_result
    )
    if(git_result EQUAL 0 AND git_output)
        set(kalve137_git_hash "${git_output}")
    endif()
endif()

target_compile_definitions(kalve137_app PUBLIC
    KALVE137_VERSION="${PROJECT_VERSION}"
    KALVE137_GIT_HASH="${kalve137_git_hash}"
    KALVE137_BUILD_TYPE="$<CONFIG>"
)
target_link_libraries(kalve137_app PUBLIC
    kalve137_platform
    kalve137_log
    kalve137_core
    kalve137_viewer
    occt_qt
    Qt6::Svg
    Qt6::Widgets
    TKBRep
    TKG3d
    TKGeomBase
    TKMath
    TKService
    TKTopAlgo
    TKV3d
)
kalve137_set_warnings(kalve137_app)

qt_add_executable(kalve137 WIN32 main.cpp)
target_link_libraries(kalve137 PRIVATE kalve137_app)
kalve137_set_warnings(kalve137)

set(asset_dir "${PROJECT_SOURCE_DIR}/assets")
set(branding_dir "${asset_dir}/branding")
if(WIN32)
    target_sources(kalve137 PRIVATE kalve137.rc)
    set_source_files_properties(kalve137.rc PROPERTIES OBJECT_DEPENDS "${branding_dir}/kalve137.ico")
endif()

set(branding_files
    kalve137.svg
    kalve137-small.svg
    kalve137-tiny.svg
    kalve137-wordmark-small.svg
)
list(TRANSFORM branding_files PREPEND "${branding_dir}/" OUTPUT_VARIABLE branding_paths)
foreach(path IN LISTS branding_paths)
    get_filename_component(name "${path}" NAME)
    set_source_files_properties("${path}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
set_source_files_properties("${asset_dir}/theme/theme.qss" PROPERTIES QT_RESOURCE_ALIAS "theme.qss")
set(notices_path "${PROJECT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md")
set_source_files_properties("${notices_path}" PROPERTIES QT_RESOURCE_ALIAS "THIRD-PARTY-NOTICES.md")

qt_add_resources(kalve137_app kalve137_resources
    PREFIX /kalve137
    BASE "${asset_dir}"
    FILES
        ${asset_dir}/theme/theme.qss
        ${asset_dir}/icons/appearance.svg
        ${asset_dir}/icons/arc.svg
        ${asset_dir}/icons/arc_through.svg
        ${asset_dir}/icons/axis.svg
        ${asset_dir}/icons/body.svg
        ${asset_dir}/icons/camera.svg
        ${asset_dir}/icons/center_rectangle.svg
        ${asset_dir}/icons/chamfer.svg
        ${asset_dir}/icons/circle.svg
        ${asset_dir}/icons/coincident.svg
        ${asset_dir}/icons/constrained_orbit.svg
        ${asset_dir}/icons/construct_point.svg
        ${asset_dir}/icons/construction.svg
        ${asset_dir}/icons/create_sketch.svg
        ${asset_dir}/icons/dimension.svg
        ${asset_dir}/icons/equal.svg
        ${asset_dir}/icons/extrude.svg
        ${asset_dir}/icons/fillet.svg
        ${asset_dir}/icons/finish_sketch.svg
        ${asset_dir}/icons/fit_all.svg
        ${asset_dir}/icons/folder.svg
        ${asset_dir}/icons/hole.svg
        ${asset_dir}/icons/home.svg
        ${asset_dir}/icons/horizontal.svg
        ${asset_dir}/icons/import.svg
        ${asset_dir}/icons/line.svg
        ${asset_dir}/icons/lock.svg
        ${asset_dir}/icons/look_at.svg
        ${asset_dir}/icons/measure.svg
        ${asset_dir}/icons/midplane.svg
        ${asset_dir}/icons/midpoint.svg
        ${asset_dir}/icons/mirror.svg
        ${asset_dir}/icons/move.svg
        ${asset_dir}/icons/offset_plane.svg
        ${asset_dir}/icons/origin.svg
        ${asset_dir}/icons/parallel.svg
        ${asset_dir}/icons/pattern.svg
        ${asset_dir}/icons/perpendicular.svg
        ${asset_dir}/icons/plane_angle.svg
        ${asset_dir}/icons/point.svg
        ${asset_dir}/icons/polygon.svg
        ${asset_dir}/icons/press_pull.svg
        ${asset_dir}/icons/qss/check.svg
        ${asset_dir}/icons/qss/check_disabled.svg
        ${asset_dir}/icons/qss/chevron_down.svg
        ${asset_dir}/icons/qss/chevron_up.svg
        ${asset_dir}/icons/qss/partial.svg
        ${asset_dir}/icons/qss/partial_disabled.svg
        ${asset_dir}/icons/rectangle.svg
        ${asset_dir}/icons/redo.svg
        ${asset_dir}/icons/revolve.svg
        ${asset_dir}/icons/select.svg
        ${asset_dir}/icons/shell.svg
        ${asset_dir}/icons/show_grid.svg
        ${asset_dir}/icons/sketch_fillet.svg
        ${asset_dir}/icons/snap_grid.svg
        ${asset_dir}/icons/symmetric.svg
        ${asset_dir}/icons/tangent.svg
        ${asset_dir}/icons/trim.svg
        ${asset_dir}/icons/undo.svg
        ${asset_dir}/icons/vertical.svg
        ${asset_dir}/icons/visual_style.svg
        ${branding_paths}
        ${notices_path}
)

set(font_dir "${PROJECT_SOURCE_DIR}/third_party/roboto")
kalve137_stage_runtime(kalve137 FONT_DIRS ${font_dir})
kalve137_install_app(kalve137 FONT_DIRS ${font_dir})
kalve137_install_notices()
install(FILES "${asset_dir}/samples/dodecahedron.k137" DESTINATION ${CMAKE_INSTALL_BINDIR}/samples)
kalve137_archive_symbols(kalve137 "${PROJECT_VERSION}" "${kalve137_git_hash}")
