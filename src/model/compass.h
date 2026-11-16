// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "model/vec.h"

#include <cmath>
#include <string>

namespace kalve137::model {

inline Vec3 compassDirection(const std::string& word)
{
    if (word == "east") {
        return {1, 0, 0};
    }
    if (word == "west") {
        return {-1, 0, 0};
    }
    if (word == "north") {
        return {0, 1, 0};
    }
    if (word == "south") {
        return {0, -1, 0};
    }
    if (word == "top") {
        return {0, 0, 1};
    }
    return {0, 0, -1};
}

inline std::string compassWord(Vec3 direction)
{
    const double ax = std::abs(direction.x);
    const double ay = std::abs(direction.y);
    const double az = std::abs(direction.z);
    if (ax >= ay && ax >= az) {
        return direction.x > 0 ? "east" : "west";
    }
    if (ay >= az) {
        return direction.y > 0 ? "north" : "south";
    }
    return direction.z > 0 ? "top" : "bottom";
}

} // namespace kalve137::model
