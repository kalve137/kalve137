# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Eigen3 CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS graph math)

set(planegcs_dir ${CMAKE_CURRENT_SOURCE_DIR}/Sketcher/App/planegcs)

add_library(planegcs SHARED
    ${planegcs_dir}/Constraints.cpp
    ${planegcs_dir}/GCS.cpp
    ${planegcs_dir}/Geo.cpp
    ${planegcs_dir}/SubSystem.cpp
    ${planegcs_dir}/qp_eq.cpp
)

set_target_properties(planegcs PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    CXX_VISIBILITY_PRESET default
)

target_include_directories(planegcs
    SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Sketcher/App
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim
)

target_compile_definitions(planegcs PUBLIC EIGEN_NO_DEBUG)
target_link_libraries(planegcs PUBLIC Eigen3::Eigen Boost::math PRIVATE Boost::graph)

if(MSVC)
    target_compile_options(planegcs PRIVATE /W0 /utf-8 /bigobj)
else()
    target_compile_options(planegcs PRIVATE -w)
endif()

install(TARGETS planegcs RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
