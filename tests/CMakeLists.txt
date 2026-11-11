# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(GoogleTest)

add_executable(kalve137_tests
    app/theme_test.cpp
    core/core_test.cpp
    core/expression_test.cpp
    core/feature_test.cpp
    core/history_test.cpp
    core/naming_test.cpp
    core/sample_test.cpp
    core/shape_store_test.cpp
    core/sweep_test.cpp
    geometry/geometry_test.cpp
    geometry/measure_test.cpp
    integration/workflow_test.cpp
    log/log_test.cpp
    platform/crash_test.cpp
    sketch/build_test.cpp
    sketch/dimension_test.cpp
    sketch/draft_test.cpp
    sketch/edit_test.cpp
    sketch/select_test.cpp
    sketch/sketch_test.cpp
    viewer/camera_guard_test.cpp
    viewer/grid_test.cpp
    viewer/view_turn_test.cpp
)
target_compile_definitions(kalve137_tests PRIVATE KALVE137_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(kalve137_tests PRIVATE
    kalve137_app
    kalve137_core
    kalve137_geometry
    kalve137_log
    kalve137_platform
    kalve137_sketch
    kalve137_viewer
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
kalve137_set_warnings(kalve137_tests)
kalve137_copy_runtime_dlls(kalve137_tests)
gtest_discover_tests(kalve137_tests DISCOVERY_MODE PRE_TEST)

find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(kalve137_uitests
    ui/crash_trace.cpp
    ui/crash_trace.h
    ui/dimension_ui_test.cpp
    ui/feature_ui_test.cpp
    ui/files_ui_test.cpp
    ui/harness_test.cpp
    ui/hero_ui_test.cpp
    ui/menu_ui_test.cpp
    ui/preferences_ui_test.cpp
    ui/scenario.cpp
    ui/select_ui_test.cpp
    ui/scenario.h
    ui/ui_driver.cpp
    ui/ui_driver.h
    ui/ui_main.cpp
    ui/ui_test.h
    ui/workflow_ui_test.cpp
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
    PROPERTIES LABELS ui RUN_SERIAL TRUE
)
