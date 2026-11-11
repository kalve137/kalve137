// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <functional>
#include <vector>

namespace kalve137::sketch {

enum class BoxMode { Window, Crossing };

using Projection = std::function<Vec2(Vec2)>;

std::vector<Vec2> polyline(const Sketch& sketch, const Curve& curve);
std::vector<Id> inBox(const Sketch& sketch, const Projection& project, Vec2 corner, Vec2 opposite, BoxMode mode);

} // namespace kalve137::sketch
