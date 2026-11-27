# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

set(KALVE137_BOUNDARY_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/check_includes.cmake")

function(kalve137_forbid target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "QT;OCCT" "" "MODULES")

    set(patterns)
    set(link_patterns)
    if(arg_QT)
        list(APPEND patterns "#include <Q" "#include <qt" "#include <rhi/")
        list(APPEND link_patterns "^Qt[0-9]*::")
    endif()
    if(arg_OCCT)
        list(APPEND patterns ".hxx>")
        list(APPEND link_patterns "^TK")
    endif()
    foreach(module IN LISTS arg_MODULES)
        list(APPEND patterns "#include \"${module}/")
    endforeach()

    get_target_property(type ${target} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY")
        get_target_property(libraries ${target} INTERFACE_LINK_LIBRARIES)
    else()
        get_target_property(libraries ${target} LINK_LIBRARIES)
    endif()
    if(libraries)
        foreach(library IN LISTS libraries)
            foreach(link_pattern IN LISTS link_patterns)
                if(library MATCHES "${link_pattern}")
                    message(FATAL_ERROR "${target} may not link ${library}")
                endif()
            endforeach()
        endforeach()
    endif()

    get_target_property(folder ${target} SOURCE_DIR)
    file(GLOB files CONFIGURE_DEPENDS "${folder}/*.h" "${folder}/*.cpp")
    set(config "${CMAKE_CURRENT_BINARY_DIR}/${target}_boundaries.cmake")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}_boundaries.stamp")
    set(body "set(TARGET [==[${target}]==])\nset(FILES)\nset(PATTERNS)\n")
    foreach(file IN LISTS files)
        string(APPEND body "list(APPEND FILES [==[${file}]==])\n")
    endforeach()
    foreach(pattern IN LISTS patterns)
        string(APPEND body "list(APPEND PATTERNS [==[${pattern}]==])\n")
    endforeach()
    file(WRITE "${config}" "${body}")

    set(dev_errors)
    if(KALVE137_WARNINGS_AS_ERRORS)
        set(dev_errors -Werror=dev)
    endif()
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND ${CMAKE_COMMAND} ${dev_errors} -DCONFIG=${config} -DSTAMP=${stamp} -P ${KALVE137_BOUNDARY_SCRIPT}
        DEPENDS ${files} "${config}" "${KALVE137_BOUNDARY_SCRIPT}"
        COMMENT "Checking includes of ${target}"
        VERBATIM
    )
    add_custom_target(${target}_boundaries ALL DEPENDS "${stamp}")
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
        add_dependencies(${target} ${target}_boundaries)
    endif()
endfunction()
