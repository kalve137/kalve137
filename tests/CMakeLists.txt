# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(GoogleTest)

add_executable(kalve137_tests
    app/badge_placement_test.cpp
    app/batch_import_test.cpp
    app/body_opacity_test.cpp
    app/design_session_test.cpp
    app/panel_picks_test.cpp
    app/startup_options_test.cpp
    app/theme_test.cpp
    app/version_test.cpp
    camera/camera_test.cpp
    camera/gravity_test.cpp
    camera/jitter_test.cpp
    camera/navigator_test.cpp
    camera/occt_compare.h
    camera/views_test.cpp
    core/core_test.cpp
    core/expression_test.cpp
    core/feature_test.cpp
    core/history_test.cpp
    core/naming_test.cpp
    core/parameter_grammar_test.cpp
    core/parameters_test.cpp
    core/projection_test.cpp
    core/same_document.h
    core/sample_test.cpp
    core/shape_store_test.cpp
    core/snapshot_cache_test.cpp
    core/sweep_test.cpp
    geometry/body_mesh_test.cpp
    geometry/box_selection_compare_test.cpp
    geometry/geometry_test.cpp
    geometry/measure_test.cpp
    integration/workflow_test.cpp
    log/log_test.cpp
    pick/ray_scene_test.cpp
    platform/crash_test.cpp
    render/box_select_test.cpp
    render/cpu_backend_test.cpp
    render/datums_test.cpp
    render/draw_list_test.cpp
    render/annotations_test.cpp
    render/edge_quad_test.cpp
    render/grid_overlay_test.cpp
    render/grid_test.cpp
    render/measure_test.cpp
    render/pick_id_test.cpp
    render/positions_test.cpp
    render/shaders_test.cpp
    render/sketches_test.cpp
    render/start_problem_test.cpp
    render/test_meshes.h
    render/transparency_test.cpp
    sketch/build_test.cpp
    sketch/dimension_test.cpp
    sketch/draft_test.cpp
    sketch/edit_test.cpp
    sketch/offset_test.cpp
    sketch/select_test.cpp
    sketch/sketch_test.cpp
    sketch/snapshot_test.cpp
    viewer/camera_guard_test.cpp
    viewer/grid_test.cpp
    viewer/view_turn_test.cpp
)
target_compile_definitions(kalve137_tests PRIVATE
    KALVE137_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    KALVE137_TEST_PROJECT_VERSION="${PROJECT_VERSION}"
    KALVE137_TEST_VERSION_BASE="${KALVE137_VERSION_BASE}"
    KALVE137_TEST_VERSION_SUFFIX="${KALVE137_VERSION_SUFFIX}"
)
target_link_libraries(kalve137_tests PRIVATE
    kalve137_app
    kalve137_camera
    kalve137_core
    kalve137_geometry
    kalve137_log
    kalve137_pick
    kalve137_platform
    kalve137_render
    kalve137_sketch
    kalve137_viewer
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    Qt6::GuiPrivate
)
qt_add_shaders(kalve137_tests "kalve137_test_shaders"
    PREFIX "/shaders"
    BASE render/shaders
    GLSL "100 es,120,150"
    HLSL 50
    MSL 12
    FILES
        render/shaders/probe.vert
        render/shaders/probe.frag
)
kalve137_set_warnings(kalve137_tests)
kalve137_copy_runtime_dlls(kalve137_tests)
gtest_discover_tests(kalve137_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)

find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(kalve137_uitests
    app/view_projection_test.cpp
    ui/badge_pair_ui_test.cpp
    ui/box_compare_ui_test.cpp
    ui/camera_ui_test.cpp
    ui/constraint_ui_test.cpp
    ui/crash_trace.cpp
    ui/crash_trace.h
    ui/dimension_ui_test.cpp
    ui/feature_ui_test.cpp
    ui/files_ui_test.cpp
    ui/grid_pair_ui_test.cpp
    ui/harness_test.cpp
    ui/edge_pair_ui_test.cpp
    ui/hero_ui_test.cpp
    ui/highlight_pair_ui_test.cpp
    ui/line_ui_test.cpp
    ui/marker_pair_ui_test.cpp
    ui/measure_ui_test.cpp
    ui/menu_ui_test.cpp
    ui/viewport_api_ui_test.cpp
    ui/viewport_camera_ui_test.cpp
    ui/viewport_draw_ui_test.cpp
    ui/viewport_handle_ui_test.cpp
    ui/viewport_pick_ui_test.cpp
    ui/viewport_scene_ui_test.cpp
    ui/viewport_transparent_ui_test.cpp
    ui/viewport_ui_support.cpp
    ui/viewport_ui_support.h
    ui/pair_shots.cpp
    ui/pair_shots.h
    ui/panel_pick_ui_test.cpp
    ui/parameters_ui_test.cpp
    ui/plane_pair_ui_test.cpp
    ui/preferences_ui_test.cpp
    ui/preview_pair_ui_test.cpp
    ui/recent_values_ui_test.cpp
    ui/scenario.cpp
    ui/select_ui_test.cpp
    ui/sketch_input_ui_test.cpp
    ui/sketch_move_ui_test.cpp
    ui/offset_ui_test.cpp
    ui/slot_ui_test.cpp
    ui/scenario.h
    ui/screen_ui_test.cpp
    ui/shading_pair_ui_test.cpp
    ui/shell_cavity_ui_test.cpp
    ui/sketch_pair_ui_test.cpp
    ui/sketch_project_ui_test.cpp
    ui/ui_driver.cpp
    ui/ui_driver.h
    ui/ui_main.cpp
    ui/ui_test.h
    ui/vertex_mark_ui_test.cpp
    ui/view_cube_ui_test.cpp
    ui/workflow_ui_test.cpp
    viewer/camera_sync_test.cpp
)
set_target_properties(kalve137_uitests PROPERTIES AUTOUIC OFF)
target_compile_definitions(kalve137_uitests PRIVATE KALVE137_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(kalve137_uitests PRIVATE
    kalve137_app
    kalve137_core
    kalve137_geometry
    kalve137_log
    kalve137_viewer
    GTest::gtest
    nlohmann_json::nlohmann_json
    Qt6::Test
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
kalve137_set_warnings(kalve137_uitests)
kalve137_copy_runtime_dlls(kalve137_uitests)
gtest_discover_tests(kalve137_uitests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS ui RUN_SERIAL TRUE
)
foreach(scale 125 150 175)
    math(EXPR whole "${scale} / 100")
    math(EXPR part "${scale} % 100")
    add_test(NAME ScreenTest.AtScale${scale} COMMAND kalve137_uitests --gtest_filter=ScreenTest.*)
    set_tests_properties(ScreenTest.AtScale${scale} PROPERTIES
        ENVIRONMENT "QT_SCALE_FACTOR=${whole}.${part};KALVE137_UI_SHOTS=$<TARGET_FILE_DIR:kalve137_uitests>/ui-shots/scale-${scale}"
        LABELS ui
        RUN_SERIAL TRUE
    )
endforeach()
add_test(NAME Viewport.Harness
    COMMAND kalve137_uitests --gtest_filter=HarnessTest.RendersTheWindowOffScreen:ViewportTest.*:ShadingPairTest.*:EdgePairTest.*:HighlightPairTest.*:GridPairTest.*:MarkerPairTest.*)
set_tests_properties(Viewport.Harness PROPERTIES
    ENVIRONMENT "KALVE137_VIEW=new;KALVE137_UI_SHOTS=$<TARGET_FILE_DIR:kalve137_uitests>/ui-shots/new"
    LABELS ui
    RUN_SERIAL TRUE
)
set(viewport_sketch
    ConstraintTest.*
    DimensionTest.*
    LineTest.*
    OffsetTest.*
    ProjectTest.*
    ScreenTest.FloorFitsTheWindowAndBothToolbars
    SelectTest.SketchWindowTakesTheWholeRectangle
    SelectTest.SketchCrossingTakesWhatItTouches
    SketchMoveTest.*
    SlotTest.*
    ValueNamesTest.*
    WorkflowTest.*
)
list(JOIN viewport_sketch ":" viewport_sketch_filter)
add_test(NAME Viewport.Sketch COMMAND kalve137_uitests --gtest_filter=${viewport_sketch_filter})
set_tests_properties(Viewport.Sketch PROPERTIES
    ENVIRONMENT "KALVE137_VIEW=new;KALVE137_UI_SHOTS=$<TARGET_FILE_DIR:kalve137_uitests>/ui-shots/new"
    LABELS ui
    RUN_SERIAL TRUE
)
add_test(NAME Viewport.Features
    COMMAND kalve137_uitests --gtest_filter=FeatureTest.*:RecentValuesTest.*:HandlePairTest.*:MeasureTest.*:PanelPickPairTest.*:PreviewPairTest.*:ShellCavityTest.*:VertexMarkTest.*:ScreenTest.LongSelectionKeepsPanelButtonsInView)
set_tests_properties(Viewport.Features PROPERTIES
    ENVIRONMENT "KALVE137_VIEW=new;KALVE137_UI_SHOTS=$<TARGET_FILE_DIR:kalve137_uitests>/ui-shots/new"
    LABELS ui
    RUN_SERIAL TRUE
)
set(viewport_pairs
    FilesTest.SavesReopensExportsAndImports
    FilesTest.OpenSampleOpensAnUntitledCopyOfTheDodecahedron
    MenuTest.TimelineSuppressesAndUnsuppresses
    MenuTest.TimelineFindsTheBodyInTheBrowser
    MenuTest.TimelineHoverHighlightsTheBody
    MenuTest.BrowserHidesAndShowsABody
    MenuTest.BrowserOpensAppearance
    MenuTest.MovesTheBlockUp
    MenuTest.PatternsTheBlockAlongX
    PlanePairTest.*
    PreferencesTest.ChoosingLightSwitchesLiveAndIsKept
    PreferencesTest.LightThemeShowsABodyASketchAndADialog
    SelectTest.FilletWindowTakesEveryVisibleEdgeOfTheBlock
    SelectTest.IdleWindowHighlightsWhatItSelects
    SelectTest.FilletCrossingAddsWithoutDroppingEarlierEdges
    SelectTest.WindowOverThousandsOfFacesStaysFast
    SketchPairTest.*
    SketchInputTest.*
    BadgePairTest.*
    ProjectPairTest.*
)
list(JOIN viewport_pairs ":" viewport_filter)
add_test(NAME Viewport.Pairs COMMAND kalve137_uitests --gtest_filter=${viewport_filter})
set_tests_properties(Viewport.Pairs PROPERTIES
    ENVIRONMENT "KALVE137_VIEW=new;KALVE137_UI_SHOTS=$<TARGET_FILE_DIR:kalve137_uitests>/ui-shots/new"
    LABELS ui
    RUN_SERIAL TRUE
)
