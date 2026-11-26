# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_build_options INTERFACE)

function(kalve137_use_build_options target)
    target_link_libraries(${target} PRIVATE kalve137_build_options)
endfunction()

if(MSVC)
    target_compile_options(kalve137_build_options INTERFACE /guard:cf)
    target_link_options(kalve137_build_options INTERFACE /guard:cf)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        target_link_options(kalve137_build_options INTERFACE /CETCOMPAT)
    endif()
else()
    include(CheckPIESupported)
    check_pie_supported()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    target_compile_options(kalve137_build_options INTERFACE
        -fstack-protector-strong
        $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(kalve137_build_options INTERFACE -fcf-protection=full)
    endif()
    if(NOT APPLE)
        target_link_options(kalve137_build_options INTERFACE -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
    endif()
endif()
