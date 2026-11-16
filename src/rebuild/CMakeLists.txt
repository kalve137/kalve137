# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(kalve137_rebuild STATIC
    outline.cpp
    outline.h
    rebuild.cpp
    rebuild.h
    sketch.cpp
    sketch.h
    suggest.cpp
    suggest.h
)
target_link_libraries(kalve137_rebuild PUBLIC kalve137_model kalve137_kernel PRIVATE kalve137_solve)
