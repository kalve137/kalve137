// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <optional>

namespace kalve137::sketch {

struct Piece {
    CurveKind kind = CurveKind::Line;
    Vec2 start;
    Vec2 end;
    Vec2 center;
    double radius = 0.0;
    double startAngle = 0.0;
    double endAngle = 0.0;
    bool whole = false;
};

std::optional<Piece> trimPiece(const Sketch& sketch, Id curve, Vec2 pick);

std::optional<Sketch> trim(const Sketch& sketch, Id curve, Vec2 pick);

std::optional<Sketch> filletCorner(const Sketch& sketch, Id point, double radius);

} // namespace kalve137::sketch
