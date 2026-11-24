// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::sketch {

inline constexpr double kDimensionFlyoutPixels = 28.0;
inline constexpr double kAngleDimensionPixels = 64.0;
inline constexpr double kAngleDimensionLegShare = 0.6;
inline constexpr double kDimensionArrowPixels = 10.0;
inline constexpr double kDimensionArrowDegrees = 12.0;
inline constexpr double kDimensionExtension = 6.0;

struct DimensionSegment {
    Vec2 from;
    Vec2 to;
};

struct DimensionArrow {
    Vec2 tip;
    Vec2 left;
    Vec2 right;
};

struct DimensionLayout {
    std::vector<DimensionSegment> lines;
    std::vector<Vec2> arc;
    std::vector<DimensionArrow> arrows;
    Vec2 text;
    std::string label;
};

std::string dimensionLabel(ConstraintKind kind, double value);
Vec2 dimensionMiddle(const Sketch& sketch);
std::optional<DimensionLayout> layoutDimension(const Sketch& sketch, const Constraint& constraint, double worldPerPixel,
                                               Vec2 middle);
std::map<Id, DimensionLayout> layoutDimensions(const Sketch& sketch, double worldPerPixel);

} // namespace kalve137::sketch
