// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <optional>
#include <utility>

namespace kalve137::sketch {

std::optional<std::pair<Vec2, Vec2>> measuredPoints(const Sketch& sketch, const Constraint& constraint);
std::optional<Vec2> dimensionAnchor(const Sketch& sketch, const Constraint& constraint);
std::optional<Vec2> labelPosition(const Sketch& sketch, const Constraint& constraint);
ConstraintKind pointDistanceKind(Vec2 a, Vec2 b, Vec2 cursor);

} // namespace kalve137::sketch
