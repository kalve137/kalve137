// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/build.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace kalve137::sketch {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double wrapped(double angle)
{
    angle = std::fmod(angle, kTwoPi);
    return angle < 0.0 ? angle + kTwoPi : angle;
}

double angleFrom(Vec2 center, Vec2 p)
{
    return std::atan2(p.y - center.y, p.x - center.x);
}

} // namespace

std::vector<Vec2> polygonCorners(Vec2 center, Vec2 firstCorner, int sides)
{
    std::vector<Vec2> corners;
    if (sides < 3) {
        return corners;
    }
    const double radius = distance(center, firstCorner);
    const double start = angleFrom(center, firstCorner);
    for (int i = 0; i < sides; ++i) {
        const double a = start + kTwoPi * i / sides;
        corners.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
    return corners;
}

std::optional<Vec2> circleThrough(Vec2 a, Vec2 b, Vec2 c)
{
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    const double scale = std::max({distance(a, b), distance(b, c), distance(a, c)});
    if (std::abs(d) < 1e-9 * scale * scale) {
        return std::nullopt;
    }
    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double c2 = c.x * c.x + c.y * c.y;
    return Vec2{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
                (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
}

RectangleSides addCenterRectangle(Sketch& sketch, Id center, Vec2 corner)
{
    const Vec2 c = sketch.point(center)->pos;
    const double dx = std::abs(corner.x - c.x);
    const double dy = std::abs(corner.y - c.y);
    const Id p0 = sketch.addPoint({c.x - dx, c.y - dy});
    const Id p1 = sketch.addPoint({c.x + dx, c.y - dy});
    const Id p2 = sketch.addPoint({c.x + dx, c.y + dy});
    const Id p3 = sketch.addPoint({c.x - dx, c.y + dy});
    RectangleSides sides;
    sides.bottom = sketch.addLine(p0, p1);
    sides.right = sketch.addLine(p1, p2);
    sides.top = sketch.addLine(p2, p3);
    sides.left = sketch.addLine(p3, p0);
    sketch.addConstraint(ConstraintKind::Horizontal, {sides.bottom});
    sketch.addConstraint(ConstraintKind::Horizontal, {sides.top});
    sketch.addConstraint(ConstraintKind::Vertical, {sides.right});
    sketch.addConstraint(ConstraintKind::Vertical, {sides.left});
    const Id diagonal = sketch.addLine(p0, p2, true);
    sketch.addLine(p1, p3, true);
    sketch.addConstraint(ConstraintKind::Midpoint, {center, diagonal});
    return sides;
}

Polygon addPolygon(Sketch& sketch, Id center, Vec2 firstCorner, int sides)
{
    Polygon polygon;
    const std::vector<Vec2> corners = polygonCorners(sketch.point(center)->pos, firstCorner, sides);
    if (corners.empty()) {
        return polygon;
    }
    polygon.circle = sketch.addCircle(center, distance(sketch.point(center)->pos, firstCorner), true);
    std::vector<Id> points;
    for (const Vec2 corner : corners) {
        points.push_back(sketch.addPoint(corner));
        sketch.addConstraint(ConstraintKind::PointOnCurve, {points.back(), polygon.circle});
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        polygon.sides.push_back(sketch.addLine(points[i], points[(i + 1) % points.size()]));
        if (i > 0) {
            sketch.addConstraint(ConstraintKind::Equal, {polygon.sides[i - 1], polygon.sides[i]});
        }
    }
    return polygon;
}

std::optional<Id> addArcThrough(Sketch& sketch, Id start, Id end, Vec2 through)
{
    const Vec2 s = sketch.point(start)->pos;
    const Vec2 e = sketch.point(end)->pos;
    const std::optional<Vec2> center = circleThrough(s, e, through);
    if (!center) {
        return std::nullopt;
    }
    const double from = angleFrom(*center, s);
    const bool forward = wrapped(angleFrom(*center, through) - from) < wrapped(angleFrom(*center, e) - from);
    const Id c = sketch.addPoint(*center);
    return forward ? sketch.addArc(c, start, end) : sketch.addArc(c, end, start);
}

std::optional<std::vector<Id>> addMirror(Sketch& sketch, const std::vector<Id>& curves, Id axis)
{
    const Curve* axisCurve = sketch.curve(axis);
    if (axisCurve == nullptr || axisCurve->kind != CurveKind::Line) {
        return std::nullopt;
    }
    const Vec2 a = sketch.point(axisCurve->points[0])->pos;
    const Vec2 b = sketch.point(axisCurve->points[1])->pos;
    const double axisLength = distance(a, b);
    if (axisLength < 1e-12) {
        return std::nullopt;
    }
    const Vec2 u = (b - a) * (1.0 / axisLength);
    const double onAxis = 1e-9 * std::max(1.0, axisLength);
    const auto reflect = [&](Vec2 p) {
        const Vec2 d = p - a;
        const Vec2 foot = a + u * (d.x * u.x + d.y * u.y);
        return foot * 2.0 - p;
    };
    const auto isOnAxis = [&](Id point) {
        const Vec2 p = sketch.point(point)->pos;
        return distance(p, reflect(p)) < onAxis;
    };

    std::map<Id, Id> copies;
    const auto mirrored = [&](Id point) {
        if (auto it = copies.find(point); it != copies.end()) {
            return it->second;
        }
        Id copy = point;
        if (!isOnAxis(point)) {
            copy = sketch.addPoint(reflect(sketch.point(point)->pos));
            sketch.addConstraint(ConstraintKind::Symmetric, {point, copy, axis});
        }
        copies.emplace(point, copy);
        return copy;
    };

    std::vector<Id> created;
    std::vector<Id> done;
    for (Id id : curves) {
        if (id == axis || !sketch.isCurve(id) || std::ranges::find(done, id) != done.end()) {
            continue;
        }
        done.push_back(id);
        const Curve source = *sketch.curve(id);
        switch (source.kind) {
        case CurveKind::Line:
            if (isOnAxis(source.points[0]) && isOnAxis(source.points[1])) {
                continue;
            }
            created.push_back(
                sketch.addLine(mirrored(source.points[0]), mirrored(source.points[1]), source.construction));
            break;
        case CurveKind::Circle:
            if (isOnAxis(source.points[0])) {
                continue;
            }
            created.push_back(sketch.addCircle(mirrored(source.points[0]), source.radius, source.construction));
            sketch.addConstraint(ConstraintKind::Equal, {id, created.back()});
            break;
        case CurveKind::Arc: {
            const Vec2 start = sketch.point(source.points[1])->pos;
            const Vec2 end = sketch.point(source.points[2])->pos;
            if (isOnAxis(source.points[0]) && distance(reflect(start), end) < onAxis) {
                continue;
            }
            created.push_back(sketch.addArc(mirrored(source.points[0]), mirrored(source.points[2]),
                                            mirrored(source.points[1]), source.construction));
            break;
        }
        }
    }
    if (created.empty()) {
        return std::nullopt;
    }
    return created;
}

} // namespace kalve137::sketch
