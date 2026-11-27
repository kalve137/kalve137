// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/dimension.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace kalve137::sketch {

namespace {

constexpr double kMinLength = 1e-9;
constexpr double kParallelSine = 1e-9;

std::optional<Vec2> pointOf(const Sketch& sketch, Id id)
{
    const Point* point = sketch.point(id);
    return point != nullptr ? std::optional<Vec2>(point->pos) : std::nullopt;
}

std::optional<std::pair<Vec2, Vec2>> lineEnds(const Sketch& sketch, Id id)
{
    const Curve* curve = sketch.curve(id);
    if (curve == nullptr || curve->kind != CurveKind::Line) {
        return std::nullopt;
    }
    const std::optional<Vec2> a = pointOf(sketch, curve->points[0]);
    const std::optional<Vec2> b = pointOf(sketch, curve->points[1]);
    if (!a || !b) {
        return std::nullopt;
    }
    return std::pair{*a, *b};
}

std::optional<Vec2> footOnLine(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double l2 = dot(ab, ab);
    if (l2 <= 0.0) {
        return std::nullopt;
    }
    const double t = dot(p - a, ab) / l2;
    return a + ab * t;
}

std::optional<Vec2> corner(const Sketch& sketch, Id first, Id second)
{
    const auto l1 = lineEnds(sketch, first);
    const auto l2 = lineEnds(sketch, second);
    if (!l1 || !l2) {
        return std::nullopt;
    }
    return lineCorner(l1->first, l1->second, l2->first, l2->second);
}

std::optional<Vec2> roundDirection(const Sketch& sketch, const Curve& curve)
{
    if (curve.kind == CurveKind::Circle) {
        return Vec2{0.0, 1.0};
    }
    const Point* center = sketch.point(curve.points[0]);
    const Point* start = sketch.point(curve.points[1]);
    const Point* end = sketch.point(curve.points[2]);
    if (center == nullptr || start == nullptr || end == nullptr) {
        return std::nullopt;
    }
    const double first = std::atan2(start->pos.y - center->pos.y, start->pos.x - center->pos.x);
    const double last = std::atan2(end->pos.y - center->pos.y, end->pos.x - center->pos.x);
    double sweep = last - first;
    if (sweep <= 0.0) {
        sweep += 2.0 * std::numbers::pi;
    }
    const double middle = first + sweep / 2.0;
    return Vec2{std::cos(middle), std::sin(middle)};
}

std::optional<std::pair<Vec2, Vec2>> offsetEnds(const Sketch& sketch, const Constraint& constraint)
{
    const std::vector<OffsetPair> pairs = offsetPairs(constraint);
    if (pairs.empty()) {
        return std::nullopt;
    }
    const Curve* source = sketch.curve(pairs.front().source);
    const Curve* offset = sketch.curve(pairs.front().offset);
    if (source == nullptr || offset == nullptr || source->kind != offset->kind) {
        return std::nullopt;
    }
    if (source->kind == CurveKind::Line) {
        const auto from = lineEnds(sketch, pairs.front().source);
        const auto to = lineEnds(sketch, pairs.front().offset);
        if (!from || !to) {
            return std::nullopt;
        }
        const Vec2 middle = (from->first + from->second) * 0.5;
        const std::optional<Vec2> foot = footOnLine(middle, to->first, to->second);
        return foot ? std::optional(std::pair{middle, *foot}) : std::nullopt;
    }
    const std::optional<Vec2> direction = roundDirection(sketch, *source);
    const std::optional<Vec2> sourceCenter = pointOf(sketch, source->points[0]);
    const std::optional<Vec2> offsetCenter = pointOf(sketch, offset->points[0]);
    if (!direction || !sourceCenter || !offsetCenter) {
        return std::nullopt;
    }
    return std::pair{*sourceCenter + *direction * source->radius, *offsetCenter + *direction * offset->radius};
}

} // namespace

std::optional<Vec2> lineCorner(Vec2 firstFrom, Vec2 firstTo, Vec2 secondFrom, Vec2 secondTo)
{
    const Vec2 da = firstTo - firstFrom;
    const Vec2 db = secondTo - secondFrom;
    if (length(da) <= kMinLength || length(db) <= kMinLength) {
        return std::nullopt;
    }
    if (std::abs(cross(unit(da), unit(db))) <= kParallelSine) {
        return std::nullopt;
    }
    return firstFrom + da * (cross(secondFrom - firstFrom, db) / cross(da, db));
}

std::optional<std::pair<Vec2, Vec2>> measuredPoints(const Sketch& sketch, const Constraint& constraint)
{
    switch (constraint.kind) {
    case ConstraintKind::Distance: {
        if (constraint.refs.size() == 1) {
            return lineEnds(sketch, constraint.refs[0]);
        }
        if (constraint.refs.size() != 2) {
            return std::nullopt;
        }
        const std::optional<Vec2> a = pointOf(sketch, constraint.refs[0]);
        if (!a) {
            return std::nullopt;
        }
        if (const std::optional<Vec2> b = pointOf(sketch, constraint.refs[1])) {
            return std::pair{*a, *b};
        }
        const auto line = lineEnds(sketch, constraint.refs[1]);
        if (!line) {
            return std::nullopt;
        }
        const std::optional<Vec2> foot = footOnLine(*a, line->first, line->second);
        return foot ? std::optional(std::pair{*a, *foot}) : std::nullopt;
    }
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance: {
        if (constraint.refs.size() != 2) {
            return std::nullopt;
        }
        const std::optional<Vec2> a = pointOf(sketch, constraint.refs[0]);
        const std::optional<Vec2> b = pointOf(sketch, constraint.refs[1]);
        return a && b ? std::optional(std::pair{*a, *b}) : std::nullopt;
    }
    case ConstraintKind::Offset:
        return offsetEnds(sketch, constraint);
    case ConstraintKind::Coincident:
    case ConstraintKind::Horizontal:
    case ConstraintKind::Vertical:
    case ConstraintKind::Parallel:
    case ConstraintKind::Perpendicular:
    case ConstraintKind::Tangent:
    case ConstraintKind::Equal:
    case ConstraintKind::PointOnCurve:
    case ConstraintKind::Midpoint:
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter:
    case ConstraintKind::Angle:
    case ConstraintKind::Symmetric:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Vec2> dimensionAnchor(const Sketch& sketch, const Constraint& constraint)
{
    switch (constraint.kind) {
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
    case ConstraintKind::Offset: {
        const auto ends = measuredPoints(sketch, constraint);
        return ends ? std::optional((ends->first + ends->second) * 0.5) : std::nullopt;
    }
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter: {
        const Curve* curve = constraint.refs.empty() ? nullptr : sketch.curve(constraint.refs[0]);
        return curve != nullptr && !curve->points.empty() ? pointOf(sketch, curve->points[0]) : std::nullopt;
    }
    case ConstraintKind::Angle:
        return constraint.refs.size() == 2 ? corner(sketch, constraint.refs[0], constraint.refs[1]) : std::nullopt;
    case ConstraintKind::Coincident:
    case ConstraintKind::Horizontal:
    case ConstraintKind::Vertical:
    case ConstraintKind::Parallel:
    case ConstraintKind::Perpendicular:
    case ConstraintKind::Tangent:
    case ConstraintKind::Equal:
    case ConstraintKind::PointOnCurve:
    case ConstraintKind::Midpoint:
    case ConstraintKind::Symmetric:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Vec2> labelPosition(const Sketch& sketch, const Constraint& constraint)
{
    if (!constraint.labelOffset) {
        return std::nullopt;
    }
    const std::optional<Vec2> anchor = dimensionAnchor(sketch, constraint);
    return anchor ? std::optional(*anchor + *constraint.labelOffset) : std::nullopt;
}

ConstraintKind pointDistanceKind(Vec2 a, Vec2 b, Vec2 cursor)
{
    const bool withinX = cursor.x >= std::min(a.x, b.x) && cursor.x <= std::max(a.x, b.x);
    const bool withinY = cursor.y >= std::min(a.y, b.y) && cursor.y <= std::max(a.y, b.y);
    if (withinX && !withinY) {
        return ConstraintKind::HorizontalDistance;
    }
    if (withinY && !withinX) {
        return ConstraintKind::VerticalDistance;
    }
    return ConstraintKind::Distance;
}

} // namespace kalve137::sketch
