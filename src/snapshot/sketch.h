// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/mesh.h"

#include <cstdint>
#include <vector>

namespace kalve137::snapshot {

enum class SketchCurveKind : std::uint8_t { Line, Circle, Arc };

struct SketchCurve {
    int id = 0;
    SketchCurveKind kind = SketchCurveKind::Line;
    bool construction = false;
    Polyline points;

    bool operator==(const SketchCurve&) const = default;
};

inline constexpr std::uint8_t kSketchPointFixed = 1;
inline constexpr std::uint8_t kSketchPointOrigin = 2;

struct SketchPoint {
    int id = 0;
    Point point{};
    std::uint8_t flags = 0;

    bool operator==(const SketchPoint&) const = default;
};

struct SketchProfiles {
    std::vector<Point> vertices;
    std::vector<Triangle> triangles;
    std::vector<std::uint32_t> profiles;
    std::vector<std::uint32_t> disks;
    std::uint32_t count = 0;

    bool operator==(const SketchProfiles&) const = default;
};

struct Sketch {
    int feature = 0;
    std::vector<SketchCurve> curves;
    std::vector<SketchPoint> points;
    SketchProfiles profiles;

    bool operator==(const Sketch&) const = default;
};

} // namespace kalve137::snapshot
