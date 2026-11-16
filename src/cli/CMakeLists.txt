# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(kalvec main.cpp)
target_link_libraries(kalvec PRIVATE kalve137_rebuild kalve137_look)
set_target_properties(kalvec PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
kalve137_copy_runtime_dlls(kalvec)
