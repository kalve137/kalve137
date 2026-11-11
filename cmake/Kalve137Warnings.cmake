# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

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
            -Wpedantic
            -Werror=switch
        )
    endif()
endfunction()
