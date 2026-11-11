// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <optional>

namespace kalve137::sketch {

Vec2 lineEnd(Vec2 start, Vec2 cursor, std::optional<Vec2> previousDir, std::optional<double> length,
             std::optional<double> interiorDegrees);

double interiorAngle(Vec2 previousDir, Vec2 dir);

Vec2 snapToGrid(Vec2 pos, double step);

} // namespace kalve137::sketch
