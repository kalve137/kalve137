// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::snapshot {

inline constexpr double kDeviationCoefficient = 0.001;
inline constexpr double kDeviationAngle = 20.0 * std::numbers::pi / 180.0;
inline constexpr double kDeviationPerBoxSide = 4.0;
inline constexpr double kEdgePrecision = 0.5;
inline constexpr double kRelativeMinSize = 0.1;
inline constexpr int kArcMinSegments = 3;

inline double chordDeflection(double boxSide)
{
    return boxSide * kDeviationCoefficient * kDeviationPerBoxSide;
}

inline int arcSegments(double radius, double sweep, double deflection)
{
    if (!(radius > 0.0) || !(sweep > 0.0) || !std::isfinite(radius) || !std::isfinite(sweep)) {
        return 1;
    }
    const double curvature = 2.0 * std::acos(std::max(1.0 - (kEdgePrecision * deflection / radius), 0.0));
    const double step = std::max(std::min(curvature, kEdgePrecision * kDeviationAngle),
                                 std::min(kRelativeMinSize * deflection / radius, std::numbers::pi / 2.0));
    if (!(step > 0.0)) {
        return kArcMinSegments;
    }
    return std::max(static_cast<int>(std::ceil(sweep / step)), kArcMinSegments);
}

} // namespace kalve137::snapshot
