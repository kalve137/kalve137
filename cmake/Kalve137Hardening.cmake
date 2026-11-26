# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

if(MSVC)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        add_link_options(/CETCOMPAT)
    endif()
else()
    include(CheckPIESupported)
    check_pie_supported()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_compile_options(
        -fstack-protector-strong
        $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        add_compile_options(-fcf-protection=full)
    endif()
    if(NOT APPLE)
        add_link_options(-Wl,-z,relro,-z,now -Wl,-z,noexecstack)
    endif()
endif()
