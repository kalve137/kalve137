// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/edit.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <vector>

namespace kalve137::sketch {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kTiny = 1e-12;
constexpr double kSamePosition = 1e-6;
constexpr double kSameParameter = 1e-7;

double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}
double cross(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}
double norm(Vec2 a)
{
    return std::hypot(a.x, a.y);
}

Vec2 unit(Vec2 a)
{
    const double n = norm(a);
    return n > kTiny ? a * (1.0 / n) : Vec2{1.0, 0.0};
}

double wrap(double angle)
{
    angle = std::fmod(angle, kTwoPi);
    return angle < 0.0 ? angle + kTwoPi : angle;
}

double angleOf(Vec2 v)
{
    return std::atan2(v.y, v.x);
}

struct Shape {
    CurveKind kind = CurveKind::Line;
    Vec2 a;
    Vec2 b;
    Vec2 center;
    double radius = 0.0;
    double start = 0.0;
    double sweep = kTwoPi;
};

std::optional<Shape> shapeOf(const Sketch& sketch, Id id)
{
    const Curve* curve = sketch.curve(id);
    if (curve == nullptr) {
        return std::nullopt;
    }
    Shape shape;
    shape.kind = curve->kind;
    switch (curve->kind) {
    case CurveKind::Line:
        shape.a = sketch.point(curve->points[0])->pos;
        shape.b = sketch.point(curve->points[1])->pos;
        return shape;
    case CurveKind::Circle:
        shape.center = sketch.point(curve->points[0])->pos;
        shape.radius = curve->radius;
        return shape;
    case CurveKind::Arc: {
        shape.center = sketch.point(curve->points[0])->pos;
        const Vec2 s = sketch.point(curve->points[1])->pos;
        const Vec2 e = sketch.point(curve->points[2])->pos;
        shape.radius = norm(s - shape.center);
        shape.start = angleOf(s - shape.center);
        shape.sweep = wrap(angleOf(e - shape.center) - shape.start);
        if (shape.sweep < kSameParameter) {
            shape.sweep = kTwoPi;
        }
        return shape;
    }
    }
    return std::nullopt;
}

double parameterOf(const Shape& shape, Vec2 p)
{
    switch (shape.kind) {
    case CurveKind::Line: {
        const Vec2 ab = shape.b - shape.a;
        const double l2 = dot(ab, ab);
        return l2 > kTiny ? dot(p - shape.a, ab) / l2 : 0.0;
    }
    case CurveKind::Circle:
        return wrap(angleOf(p - shape.center));
    case CurveKind::Arc:
        return wrap(angleOf(p - shape.center) - shape.start);
    }
    return 0.0;
}

bool contains(const Shape& shape, Vec2 p)
{
    switch (shape.kind) {
    case CurveKind::Line: {
        const double length = norm(shape.b - shape.a);
        const double slack = length > kTiny ? kSamePosition / length : 0.0;
        const double t = parameterOf(shape, p);
        return t >= -slack && t <= 1.0 + slack;
    }
    case CurveKind::Circle:
        return true;
    case CurveKind::Arc: {
        const double slack = shape.radius > kTiny ? kSamePosition / shape.radius : 0.0;
        const double u = parameterOf(shape, p);
        return u <= shape.sweep + slack || u >= kTwoPi - slack;
    }
    }
    return false;
}

bool isRound(CurveKind kind)
{
    return kind != CurveKind::Line;
}

std::vector<Vec2> rawCrossings(const Shape& p, const Shape& q)
{
    std::vector<Vec2> out;
    if (!isRound(p.kind) && !isRound(q.kind)) {
        const Vec2 r = p.b - p.a;
        const Vec2 s = q.b - q.a;
        const double denominator = cross(r, s);
        if (std::abs(denominator) > kTiny) {
            const double t = cross(q.a - p.a, s) / denominator;
            out.push_back(p.a + r * t);
        }
        return out;
    }
    if (isRound(p.kind) != isRound(q.kind)) {
        const Shape& line = isRound(p.kind) ? q : p;
        const Shape& circle = isRound(p.kind) ? p : q;
        const Vec2 d = unit(line.b - line.a);
        const Vec2 f = line.a - circle.center;
        const double b = dot(f, d);
        const double c = dot(f, f) - circle.radius * circle.radius;
        const double discriminant = b * b - c;
        if (discriminant < -kSamePosition) {
            return out;
        }
        const double root = std::sqrt(std::max(0.0, discriminant));
        out.push_back(line.a + d * (-b - root));
        if (root > kSamePosition) {
            out.push_back(line.a + d * (-b + root));
        }
        return out;
    }
    const Vec2 between = q.center - p.center;
    const double distance = norm(between);
    if (distance < kTiny || distance > p.radius + q.radius + kSamePosition ||
        distance < std::abs(p.radius - q.radius) - kSamePosition) {
        return out;
    }
    const double along = (distance * distance + p.radius * p.radius - q.radius * q.radius) / (2.0 * distance);
    const double h = std::sqrt(std::max(0.0, p.radius * p.radius - along * along));
    const Vec2 u = between * (1.0 / distance);
    const Vec2 base = p.center + u * along;
    const Vec2 normal{-u.y, u.x};
    out.push_back(base + normal * h);
    if (h > kSamePosition) {
        out.push_back(base - normal * h);
    }
    return out;
}

struct Cut {
    double parameter = 0.0;
    Vec2 pos;
    Id by = 0;
};

std::vector<Cut> cutsOf(const Sketch& sketch, Id id, const Shape& shape)
{
    std::vector<Cut> cuts;
    for (const auto& [otherId, other] : sketch.curves()) {
        if (otherId == id) {
            continue;
        }
        const std::optional<Shape> otherShape = shapeOf(sketch, otherId);
        if (!otherShape) {
            continue;
        }
        for (const Vec2 p : rawCrossings(shape, *otherShape)) {
            if (!contains(shape, p) || !contains(*otherShape, p)) {
                continue;
            }
            const double parameter = parameterOf(shape, p);
            switch (shape.kind) {
            case CurveKind::Line: {
                const double slack = kSamePosition / std::max(kTiny, norm(shape.b - shape.a));
                if (parameter <= slack || parameter >= 1.0 - slack) {
                    continue;
                }
                break;
            }
            case CurveKind::Arc: {
                const double slack = kSamePosition / std::max(kTiny, shape.radius);
                if (parameter <= slack || parameter >= shape.sweep - slack) {
                    continue;
                }
                break;
            }
            case CurveKind::Circle:
                break;
            }
            cuts.push_back({parameter, p, otherId});
        }
    }
    std::ranges::sort(cuts, {}, &Cut::parameter);
    auto same = [](const Cut& x, const Cut& y) { return std::abs(x.parameter - y.parameter) < kSameParameter; };
    cuts.erase(std::unique(cuts.begin(), cuts.end(), same), cuts.end());
    return cuts;
}

struct Split {
    std::optional<Cut> low;
    std::optional<Cut> high;
    bool whole = false;
};

Split splitAt(const Shape& shape, const std::vector<Cut>& cuts, Vec2 pick)
{
    Split split;
    const double at = parameterOf(shape, pick);
    if (shape.kind == CurveKind::Circle) {
        if (cuts.size() < 2) {
            split.whole = true;
            return split;
        }
        auto above = std::ranges::find_if(cuts, [&](const Cut& cut) { return cut.parameter > at; });
        split.high = above == cuts.end() ? cuts.front() : *above;
        split.low = above == cuts.begin() ? cuts.back() : *std::prev(above);
        return split;
    }
    for (const Cut& cut : cuts) {
        if (cut.parameter < at) {
            split.low = cut;
        } else if (!split.high) {
            split.high = cut;
        }
    }
    split.whole = !split.low && !split.high;
    return split;
}

Id endAt(Sketch& sketch, const Cut& cut)
{
    for (const auto& [pointId, point] : sketch.points()) {
        if (distance(point.pos, cut.pos) < kSamePosition) {
            return pointId;
        }
    }
    const Id point = sketch.addPoint(cut.pos);
    sketch.addConstraint(ConstraintKind::PointOnCurve, {point, cut.by});
    return point;
}

bool refersTo(const Constraint& constraint, Id id)
{
    return std::ranges::find(constraint.refs, id) != constraint.refs.end();
}

void dropLengthConstraints(Sketch& sketch, Id curve)
{
    std::vector<Id> stale;
    for (const auto& [constraintId, constraint] : sketch.constraints()) {
        if (!refersTo(constraint, curve)) {
            continue;
        }
        switch (constraint.kind) {
        case ConstraintKind::Distance:
        case ConstraintKind::HorizontalDistance:
        case ConstraintKind::VerticalDistance:
        case ConstraintKind::Midpoint:
            stale.push_back(constraintId);
            break;
        case ConstraintKind::Equal:
            if (sketch.curve(curve)->kind == CurveKind::Line) {
                stale.push_back(constraintId);
            }
            break;
        case ConstraintKind::Coincident:
        case ConstraintKind::Horizontal:
        case ConstraintKind::Vertical:
        case ConstraintKind::Parallel:
        case ConstraintKind::Perpendicular:
        case ConstraintKind::Tangent:
        case ConstraintKind::PointOnCurve:
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter:
        case ConstraintKind::Angle:
        case ConstraintKind::Symmetric:
            break;
        }
    }
    for (Id id : stale) {
        sketch.remove(id);
    }
}

void dropIfUnused(Sketch& sketch, Id point)
{
    if (point != kOriginId && sketch.isPoint(point) && sketch.curvesUsing(point).empty()) {
        sketch.remove(point);
    }
}

void dropOwnEnds(Sketch& sketch)
{
    std::vector<Id> stale;
    for (const auto& [constraintId, constraint] : sketch.constraints()) {
        if (constraint.kind != ConstraintKind::PointOnCurve || constraint.refs.size() != 2) {
            continue;
        }
        const Curve* curve = sketch.curve(constraint.refs[1]);
        if (curve == nullptr || curve->kind == CurveKind::Circle) {
            continue;
        }
        const auto first = curve->points.begin() + (curve->kind == CurveKind::Arc ? 1 : 0);
        if (std::find(first, curve->points.end(), constraint.refs[0]) != curve->points.end()) {
            stale.push_back(constraintId);
        }
    }
    for (Id id : stale) {
        sketch.remove(id);
    }
}

void copyDirection(Sketch& sketch, Id from, Id to)
{
    std::vector<ConstraintKind> kinds;
    for (const auto& [constraintId, constraint] : sketch.constraints()) {
        if (constraint.refs.size() == 1 && constraint.refs[0] == from &&
            (constraint.kind == ConstraintKind::Horizontal || constraint.kind == ConstraintKind::Vertical)) {
            kinds.push_back(constraint.kind);
        }
    }
    for (ConstraintKind kind : kinds) {
        sketch.addConstraint(kind, {to});
    }
}

} // namespace

std::optional<Piece> trimPiece(const Sketch& sketch, Id curve, Vec2 pick)
{
    const std::optional<Shape> shape = shapeOf(sketch, curve);
    if (!shape) {
        return std::nullopt;
    }
    const Split split = splitAt(*shape, cutsOf(sketch, curve, *shape), pick);
    Piece piece;
    piece.kind = shape->kind;
    piece.whole = split.whole;
    switch (shape->kind) {
    case CurveKind::Line:
        piece.start = split.low ? split.low->pos : shape->a;
        piece.end = split.high ? split.high->pos : shape->b;
        return piece;
    case CurveKind::Circle:
    case CurveKind::Arc:
        piece.center = shape->center;
        piece.radius = shape->radius;
        if (split.whole && shape->kind == CurveKind::Circle) {
            piece.startAngle = 0.0;
            piece.endAngle = kTwoPi;
            return piece;
        }
        if (shape->kind == CurveKind::Circle) {
            if (!split.low || !split.high) {
                return std::nullopt;
            }
            piece.startAngle = split.low->parameter;
            piece.endAngle = split.high->parameter;
            return piece;
        }
        piece.startAngle = shape->start + (split.low ? split.low->parameter : 0.0);
        piece.endAngle = shape->start + (split.high ? split.high->parameter : shape->sweep);
        return piece;
    }
    return std::nullopt;
}

std::optional<Sketch> trim(const Sketch& sketch, Id curve, Vec2 pick)
{
    const std::optional<Shape> shape = shapeOf(sketch, curve);
    if (!shape) {
        return std::nullopt;
    }
    const Split split = splitAt(*shape, cutsOf(sketch, curve, *shape), pick);
    Sketch out = sketch;
    if (split.whole) {
        out.remove(curve);
        return out;
    }
    const Curve original = *out.curve(curve);
    const std::optional<Id> low = split.low ? std::optional(endAt(out, *split.low)) : std::nullopt;
    const std::optional<Id> high = split.high ? std::optional(endAt(out, *split.high)) : std::nullopt;

    switch (original.kind) {
    case CurveKind::Line: {
        dropLengthConstraints(out, curve);
        const Id start = original.points[0];
        const Id end = original.points[1];
        if (low && high) {
            out.curve(curve)->points[1] = *low;
            const Id piece = out.addLine(*high, end, original.construction);
            copyDirection(out, curve, piece);
        } else if (low) {
            out.curve(curve)->points[1] = *low;
            dropIfUnused(out, end);
        } else {
            out.curve(curve)->points[0] = *high;
            dropIfUnused(out, start);
        }
        dropOwnEnds(out);
        return out;
    }
    case CurveKind::Arc: {
        const Id center = original.points[0];
        const Id start = original.points[1];
        const Id end = original.points[2];
        if (low && high) {
            out.curve(curve)->points[2] = *low;
            const Id piece = out.addArc(center, *high, end, original.construction);
            out.addConstraint(ConstraintKind::Equal, {curve, piece});
        } else if (low) {
            out.curve(curve)->points[2] = *low;
            dropIfUnused(out, end);
        } else {
            out.curve(curve)->points[1] = *high;
            dropIfUnused(out, start);
        }
        dropOwnEnds(out);
        return out;
    }
    case CurveKind::Circle: {
        const Id arc = out.addArc(original.points[0], *high, *low, original.construction);
        std::vector<std::pair<Id, Constraint>> moved;
        for (const auto& [constraintId, constraint] : out.constraints()) {
            if (refersTo(constraint, curve)) {
                Constraint copy = constraint;
                std::ranges::replace(copy.refs, curve, arc);
                moved.emplace_back(constraintId, std::move(copy));
            }
        }
        for (auto& [constraintId, constraint] : moved) {
            *out.constraint(constraintId) = std::move(constraint);
        }
        out.remove(curve);
        dropOwnEnds(out);
        return out;
    }
    }
    return std::nullopt;
}

std::optional<Sketch> filletCorner(const Sketch& sketch, Id point, double radius)
{
    const Point* corner = sketch.point(point);
    if (corner == nullptr || radius <= 0.0) {
        return std::nullopt;
    }
    const std::vector<Id> curves = sketch.curvesUsing(point);
    if (curves.size() != 2) {
        return std::nullopt;
    }
    Id lines[2];
    Vec2 directions[2];
    double lengths[2];
    for (int i = 0; i < 2; ++i) {
        const Curve* curve = sketch.curve(curves[i]);
        if (curve->kind != CurveKind::Line) {
            return std::nullopt;
        }
        const Id far = curve->points[0] == point ? curve->points[1] : curve->points[0];
        const Vec2 toFar = sketch.point(far)->pos - corner->pos;
        lines[i] = curves[i];
        lengths[i] = norm(toFar);
        directions[i] = unit(toFar);
    }
    const double cosine = std::clamp(dot(directions[0], directions[1]), -1.0, 1.0);
    const double angle = std::acos(cosine);
    if (angle < 1e-3 || angle > std::numbers::pi - 1e-3) {
        return std::nullopt;
    }
    const double setback = radius / std::tan(angle / 2.0);
    if (setback >= lengths[0] || setback >= lengths[1]) {
        return std::nullopt;
    }
    const Vec2 tangent0 = corner->pos + directions[0] * setback;
    const Vec2 tangent1 = corner->pos + directions[1] * setback;
    const Vec2 bisector = unit(directions[0] + directions[1]);
    const Vec2 center = corner->pos + bisector * (radius / std::sin(angle / 2.0));

    Sketch out = sketch;
    for (int i = 0; i < 2; ++i) {
        const Curve* curve = out.curve(lines[i]);
        const Id far = curve->points[0] == point ? curve->points[1] : curve->points[0];
        std::vector<Id> dimensions;
        for (const auto& [constraintId, constraint] : out.constraints()) {
            if (constraint.kind == ConstraintKind::Distance && constraint.refs == std::vector<Id>{lines[i]}) {
                dimensions.push_back(constraintId);
            }
        }
        for (Id id : dimensions) {
            out.constraint(id)->refs = {far, point};
        }
        dropLengthConstraints(out, lines[i]);
    }
    const bool keepCorner = point == kOriginId || std::ranges::any_of(out.constraints(), [&](const auto& entry) {
                                return refersTo(entry.second, point);
                            });
    const Id t0 = out.addPoint(tangent0);
    const Id t1 = out.addPoint(tangent1);
    const Id c = out.addPoint(center);
    const bool counterclockwise = cross(tangent0 - center, tangent1 - center) > 0.0;
    const Id arc = counterclockwise ? out.addArc(c, t0, t1) : out.addArc(c, t1, t0);
    const Id ends[2] = {t0, t1};
    for (int i = 0; i < 2; ++i) {
        std::vector<Id>& points = out.curve(lines[i])->points;
        std::ranges::replace(points, point, ends[i]);
        out.addConstraint(ConstraintKind::Tangent, {lines[i], arc});
    }
    out.addConstraint(ConstraintKind::Radius, {arc}, radius);
    if (keepCorner) {
        out.addConstraint(ConstraintKind::PointOnCurve, {point, lines[0]});
        out.addConstraint(ConstraintKind::PointOnCurve, {point, lines[1]});
    } else {
        dropIfUnused(out, point);
    }
    return out;
}

} // namespace kalve137::sketch
