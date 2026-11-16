# SPDX-FileCopyrightText: 2026 Danius Kalvaitis
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(nlohmann_json CONFIG REQUIRED)

add_library(kalve137_model STATIC
    json.cpp
    json.h
    select.cpp
    select.h
    snapshot.h
)
target_link_libraries(kalve137_model PUBLIC kalve137_lang nlohmann_json::nlohmann_json)
