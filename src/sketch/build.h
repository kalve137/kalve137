// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <optional>
#include <vector>

namespace kalve137::sketch {

struct RectangleSides {
    Id bottom = 0;
    Id right = 0;
    Id top = 0;
    Id left = 0;
};

struct Polygon {
    Id circle = 0;
    std::vector<Id> sides;
};

std::vector<Vec2> polygonCorners(Vec2 center, Vec2 firstCorner, int sides);

std::optional<Vec2> circleThrough(Vec2 a, Vec2 b, Vec2 c);

RectangleSides addCenterRectangle(Sketch& sketch, Id center, Vec2 corner);

Polygon addPolygon(Sketch& sketch, Id center, Vec2 firstCorner, int sides);

std::optional<Id> addArcThrough(Sketch& sketch, Id start, Id end, Vec2 through);

std::optional<std::vector<Id>> addMirror(Sketch& sketch, const std::vector<Id>& curves, Id axis);

} // namespace kalve137::sketch
