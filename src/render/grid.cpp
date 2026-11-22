// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/grid.h"

#include <algorithm>
#include <cmath>

namespace kalve137::render {

namespace {

constexpr double kGridRecenterFraction = 0.1;
constexpr double kGridShrinkFraction = 0.6;

double smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

double niceGridStep(double minimum)
{
    const double decade = std::pow(10.0, std::floor(std::log10(minimum)));
    for (const double factor : {1.0, 2.0, 5.0}) {
        if (decade * factor >= minimum) {
            return decade * factor;
        }
    }
    return decade * 10.0;
}

double gridStep(bool adaptive, double spacing, double minimum)
{
    if (adaptive || spacing <= 0.0) {
        return niceGridStep(minimum);
    }
    double step = spacing;
    while (step < minimum) {
        step *= kGridMajorEvery;
    }
    return step;
}

GridLayout layoutGrid(const GridPlane& plane, double step, const camera::Vec3& viewTarget, double viewRadius,
                      const std::optional<GridExtent>& extent)
{
    double radius = viewRadius;
    camera::Vec3 target = viewTarget;
    if (extent && extent->radius < viewRadius) {
        radius = extent->radius;
        target = extent->center;
    }
    const camera::Vec3 toTarget = target - plane.origin;
    const double majorStep = step * kGridMajorEvery;
    const GridPoint center{std::round(camera::dot(toTarget, plane.x) / majorStep) * majorStep,
                           std::round(camera::dot(toTarget, plane.y) / majorStep) * majorStep};
    return {step, center, radius};
}

bool isGridStale(const GridLayout& current, const GridLayout& wanted)
{
    const double moved = std::hypot(wanted.center.u - current.center.u, wanted.center.v - current.center.v);
    return wanted.step != current.step || wanted.radius > current.radius ||
           wanted.radius < current.radius * kGridShrinkFraction || moved > current.radius * kGridRecenterFraction;
}

std::vector<GridLine> gridLines(const GridLayout& layout)
{
    std::vector<GridLine> lines;
    if (layout.step <= 0.0 || layout.radius <= 0.0) {
        return lines;
    }
    const int half = static_cast<int>(std::ceil(layout.radius / layout.step));
    for (const bool alongV : {true, false}) {
        const double center = alongV ? layout.center.u : layout.center.v;
        const double middle = alongV ? layout.center.v : layout.center.u;
        for (int i = -half; i <= half; ++i) {
            const double offset = center + i * layout.step;
            if (std::abs(offset - center) >= layout.radius) {
                continue;
            }
            const long long index = std::llround(offset / layout.step);
            const double snapped = static_cast<double>(index) * layout.step;
            const double across = snapped - center;
            const double reach = std::sqrt(std::max(0.0, layout.radius * layout.radius - across * across));
            const GridLineKind kind = index == 0                     ? GridLineKind::Axis
                                      : index % kGridMajorEvery == 0 ? GridLineKind::Major
                                                                     : GridLineKind::Minor;
            lines.push_back({kind, alongV, snapped, middle - reach, middle + reach});
        }
    }
    return lines;
}

double gridFade(const GridLayout& layout, double u, double v)
{
    const double distance = std::hypot(u - layout.center.u, v - layout.center.v) / layout.radius;
    return smoothstep((distance - kGridFadeStart) / (1.0 - kGridFadeStart));
}

camera::Vec3 gridPoint(const GridPlane& plane, double u, double v)
{
    return plane.origin + plane.x * u + plane.y * v;
}

camera::Vec3 gridAxisDirection(const GridPlane& plane, const GridLine& line)
{
    return line.alongV ? plane.y : plane.x;
}

} // namespace kalve137::render
