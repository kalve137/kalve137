// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/math.h"

#include <optional>
#include <vector>

namespace kalve137::render {

inline constexpr int kGridMajorEvery = 5;
inline constexpr int kGridPiecesPerLine = 24;
inline constexpr double kGridMinCellPixels = 12.0;
inline constexpr double kGridRadiusFactor = 2.5;
inline constexpr double kGridDepthPixels = 1.0;
inline constexpr double kGridFadeStart = 0.35;

struct GridPlane {
    camera::Vec3 origin;
    camera::Vec3 x{1.0, 0.0, 0.0};
    camera::Vec3 y{0.0, 1.0, 0.0};
    camera::Vec3 normal{0.0, 0.0, 1.0};

    bool operator==(const GridPlane&) const = default;
};

struct GridPoint {
    double u = 0.0;
    double v = 0.0;
};

struct GridExtent {
    camera::Vec3 center;
    double radius = 0.0;

    bool operator==(const GridExtent&) const = default;
};

struct GridLayout {
    double step = 0.0;
    GridPoint center;
    double radius = 0.0;
};

enum class GridLineKind { Minor, Major, Axis };

struct GridLine {
    GridLineKind kind = GridLineKind::Minor;
    bool alongV = false;
    double offset = 0.0;
    double start = 0.0;
    double end = 0.0;
};

double niceGridStep(double minimum);
double gridStep(bool adaptive, double spacing, double minimum);
GridLayout layoutGrid(const GridPlane& plane, double step, const camera::Vec3& viewTarget, double viewRadius,
                      const std::optional<GridExtent>& extent);
bool isGridStale(const GridLayout& current, const GridLayout& wanted);
std::vector<GridLine> gridLines(const GridLayout& layout);
double gridFade(const GridLayout& layout, double u, double v);
camera::Vec3 gridPoint(const GridPlane& plane, double u, double v);
camera::Vec3 gridAxisDirection(const GridPlane& plane, const GridLine& line);

} // namespace kalve137::render
