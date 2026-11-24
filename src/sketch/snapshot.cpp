// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/snapshot.h"

#include "snapshot/chord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>

namespace kalve137::sketch {

namespace {

constexpr double kMinLength = 1e-7;
constexpr double kTurn = 2.0 * std::numbers::pi;
constexpr int kQuarters = 4;

struct Span {
    Vec2 center;
    double radius = 0.0;
    double start = 0.0;
    double sweep = 0.0;
};

struct Extent {
    std::optional<Vec2> low;
    Vec2 high;

    void add(Vec2 p)
    {
        if (!low) {
            low = p;
            high = p;
            return;
        }
        low = Vec2{std::min(low->x, p.x), std::min(low->y, p.y)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y)};
    }

    double side() const { return low ? std::max(high.x - low->x, high.y - low->y) : 0.0; }
};

Vec2 posOf(const Sketch& sketch, const Curve& curve, std::size_t index)
{
    return sketch.point(curve.points[index])->pos;
}

Vec2 around(const Span& span, double angle)
{
    return {span.center.x + (span.radius * std::cos(angle)), span.center.y + (span.radius * std::sin(angle))};
}

std::optional<Span> spanOf(const Sketch& sketch, const Curve& curve)
{
    if (curve.kind == CurveKind::Line || curve.radius < kMinLength) {
        return std::nullopt;
    }
    const Vec2 center = posOf(sketch, curve, 0);
    if (curve.kind == CurveKind::Circle) {
        return Span{.center = center, .radius = curve.radius, .start = 0.0, .sweep = kTurn};
    }
    const Vec2 from = posOf(sketch, curve, 1);
    const Vec2 to = posOf(sketch, curve, 2);
    const double start = std::atan2(from.y - center.y, from.x - center.x);
    double end = std::atan2(to.y - center.y, to.x - center.x);
    if (end <= start) {
        end += kTurn;
    }
    return Span{.center = center, .radius = curve.radius, .start = start, .sweep = end - start};
}

void extend(Extent& extent, const Sketch& sketch, const Curve& curve)
{
    if (curve.kind == CurveKind::Line) {
        const Vec2 a = posOf(sketch, curve, 0);
        const Vec2 b = posOf(sketch, curve, 1);
        if (distance(a, b) >= kMinLength) {
            extent.add(a);
            extent.add(b);
        }
        return;
    }
    const std::optional<Span> span = spanOf(sketch, curve);
    if (!span) {
        return;
    }
    extent.add(around(*span, span->start));
    extent.add(around(*span, span->start + span->sweep));
    const double quarter = kTurn / kQuarters;
    const double first = std::ceil(span->start / quarter) * quarter;
    for (int step = 0; step < kQuarters; ++step) {
        const double angle = first + (step * quarter);
        if (angle - span->start <= span->sweep) {
            extent.add(around(*span, angle));
        }
    }
}

snapshot::SketchCurveKind kindOf(CurveKind kind)
{
    switch (kind) {
    case CurveKind::Line:
        return snapshot::SketchCurveKind::Line;
    case CurveKind::Circle:
        return snapshot::SketchCurveKind::Circle;
    case CurveKind::Arc:
        return snapshot::SketchCurveKind::Arc;
    }
    return snapshot::SketchCurveKind::Line;
}

snapshot::Polyline polylineOf(const Sketch& sketch, const Curve& curve, const Placement& placement, double deflection)
{
    if (curve.kind == CurveKind::Line) {
        const Vec2 a = posOf(sketch, curve, 0);
        const Vec2 b = posOf(sketch, curve, 1);
        if (distance(a, b) < kMinLength) {
            return {};
        }
        return {placed(placement, a), placed(placement, b)};
    }
    const std::optional<Span> span = spanOf(sketch, curve);
    if (!span) {
        return {};
    }
    const int segments = snapshot::arcSegments(span->radius, span->sweep, deflection);
    snapshot::Polyline points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = span->start + (span->sweep * i / segments);
        points.push_back(placed(placement, around(*span, angle)));
    }
    if (curve.kind == CurveKind::Circle) {
        points.back() = points.front();
    }
    return points;
}

} // namespace

snapshot::Point placed(const Placement& placement, Vec2 p)
{
    snapshot::Point world{};
    for (std::size_t axis = 0; axis < world.size(); ++axis) {
        world.at(axis) = placement.origin.at(axis) + (placement.x.at(axis) * p.x) + (placement.y.at(axis) * p.y);
    }
    return world;
}

double curveDeflection(const Sketch& sketch)
{
    Extent extent;
    for (const auto& [id, curve] : sketch.curves()) {
        extend(extent, sketch, curve);
    }
    return snapshot::chordDeflection(extent.side());
}

snapshot::Sketch snapshotOf(const Sketch& sketch, const Placement& placement, int feature)
{
    snapshot::Sketch result;
    result.feature = feature;
    const double deflection = curveDeflection(sketch);
    result.curves.reserve(sketch.curves().size());
    for (const auto& [id, curve] : sketch.curves()) {
        snapshot::Polyline points = polylineOf(sketch, curve, placement, deflection);
        if (points.empty()) {
            continue;
        }
        result.curves.push_back(
            {.id = id, .kind = kindOf(curve.kind), .construction = curve.construction, .points = std::move(points)});
    }
    result.points = pointsOf(sketch, placement);
    return result;
}

std::vector<snapshot::SketchPoint> pointsOf(const Sketch& sketch, const Placement& placement)
{
    std::vector<snapshot::SketchPoint> points;
    points.reserve(sketch.points().size());
    for (const auto& [id, point] : sketch.points()) {
        const auto flags = static_cast<std::uint8_t>((point.fixed ? snapshot::kSketchPointFixed : 0U) |
                                                     (id == kOriginId ? snapshot::kSketchPointOrigin : 0U));
        points.push_back({.id = id, .point = placed(placement, point.pos), .flags = flags});
    }
    return points;
}

} // namespace kalve137::sketch
