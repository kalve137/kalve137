# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

include(GNUInstallDirs)

set_property(GLOBAL APPEND PROPERTY JOB_POOLS runtime_copy=1)
set(CMAKE_JOB_POOL_LINK runtime_copy)

function(kalve137_copy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS
    )
endfunction()

function(kalve137_stage_runtime target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "FONT_DIRS")
    foreach(dir IN LISTS arg_FONT_DIRS)
        get_filename_component(name "${dir}" NAME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${dir}" "$<TARGET_FILE_DIR:${target}>/fonts/${name}"
            VERBATIM
        )
    endforeach()

    kalve137_copy_runtime_dlls(${target})
    if(NOT WIN32)
        return()
    endif()

    get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    file(GENERATE
        OUTPUT "$<TARGET_FILE_DIR:${target}>/qt.conf"
        CONTENT "[Paths]\nPrefix = ${qt_prefix}\n"
    )
endfunction()

function(kalve137_install_app target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "FONT_DIRS")
    install(TARGETS ${target}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
    )
    foreach(dir IN LISTS arg_FONT_DIRS)
        install(DIRECTORY "${dir}" DESTINATION ${CMAKE_INSTALL_BINDIR}/fonts)
    endforeach()

    qt_generate_deploy_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
        NO_COMPILER_RUNTIME
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_TOOL_OPTIONS --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler
    )
    install(SCRIPT ${deploy_script})

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
    include(InstallRequiredSystemLibraries)
endfunction()

function(kalve137_install_notices)
    set(notices ${CMAKE_INSTALL_BINDIR}/notices)
    install(FILES "${PROJECT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md" DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${notices} RENAME "Kalve137.txt")
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/planegcs/LICENSE" DESTINATION ${notices} RENAME "PlaneGCS.txt")
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/occt_qt/LICENSE.txt" DESTINATION ${notices}
        RENAME "occt-qt-tools.txt"
    )
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/roboto/OFL.txt" DESTINATION ${notices} RENAME "Roboto.txt")

    get_filename_component(qt_licenses "${Qt6_DIR}/../../../../../Licenses/LICENSE" ABSOLUTE)
    if(EXISTS "${qt_licenses}")
        install(FILES "${qt_licenses}" DESTINATION ${notices} RENAME "Qt.txt")
    else()
        message(WARNING "No Qt license text at ${qt_licenses}")
    endif()

    set(share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    set(ports
        opencascade OpenCASCADE boost-graph Boost eigen3 Eigen spdlog spdlog fmt fmt nlohmann-json nlohmann-json
        miniz miniz freetype FreeType libpng libpng zlib zlib bzip2 bzip2 brotli Brotli
    )
    while(ports)
        list(POP_FRONT ports port name)
        if(EXISTS "${share}/${port}/copyright")
            install(FILES "${share}/${port}/copyright" DESTINATION ${notices} RENAME "${name}.txt")
        else()
            message(WARNING "No license text for ${port} in ${share}")
        endif()
    endwhile()
endfunction()

function(kalve137_archive_symbols target version hash)
    if(NOT MSVC)
        return()
    endif()

    set(release $<CONFIG:RelWithDebInfo>)
    set(folder "${CMAKE_BINARY_DIR}/symbols/${version}-${hash}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E $<IF:${release},make_directory,true> "${folder}"
        COMMAND ${CMAKE_COMMAND} -E $<IF:${release},copy_if_different,true>
            $<TARGET_FILE:${target}> $<TARGET_PDB_FILE:${target}> "${folder}"
        VERBATIM
    )
endfunction()
