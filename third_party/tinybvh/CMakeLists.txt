# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: MIT

add_library(tinybvh STATIC
    tiny_bvh.h
    tiny_bvh_base.h
    tiny_bvh_arm_double.h
    tiny_bvh_arm_float.h
    tiny_bvh_x86_double.h
    tiny_bvh_x86_float.h
    tinybvh.cpp
)
target_include_directories(tinybvh SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinybvh PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(tinybvh PRIVATE /W0)
else()
    target_compile_options(tinybvh PRIVATE -w)
endif()
