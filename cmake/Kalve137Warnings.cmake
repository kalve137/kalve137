# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

option(KALVE137_WARNINGS_AS_ERRORS "Fail the build on compiler warnings in our targets" OFF)

function(kalve137_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /external:W0
            /we4062
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wno-missing-field-initializers
            -Wpedantic
            -Werror=switch
        )
    endif()
    kalve137_use_build_options(${target})
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ${KALVE137_WARNINGS_AS_ERRORS})
    get_target_property(type ${target} TYPE)
    if(APPLE AND type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE LINKER:-no_warn_duplicate_libraries)
    endif()
endfunction()
