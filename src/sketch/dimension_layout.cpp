// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/dimension_layout.h"

#include "sketch/dimension.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace kalve137::sketch {

namespace {

constexpr double kMinLength = 1e-9;
constexpr double kOnLine = 1e-7;
constexpr double kParallelCross = 1e-12;
constexpr double kHalfTurn = std::numbers::pi;
constexpr double kArcPointsPerHalfTurn = 50.0;
constexpr int kMinArcPoints = 4;
constexpr int kLabelDecimals = 3;
constexpr const char* kRadiusPrefix = "R";
constexpr const char* kDiameterPrefix = "\xC3\x98";
constexpr const char* kDegreeSuffix = "\xC2\xB0";

enum class Place { Center, Left, Right, Fit };

double dot(Vec2 a, Vec2 b)
{
    return (a.x * b.x) + (a.y * b.y);
}

double cross(Vec2 a, Vec2 b)
{
    return (a.x * b.y) - (a.y * b.x);
}

double length(Vec2 a)
{
    return std::hypot(a.x, a.y);
}

Vec2 unit(Vec2 a)
{
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : Vec2{1.0, 0.0};
}

Vec2 leftOf(Vec2 a)
{
    return {-a.y, a.x};
}

bool finite(Vec2 a)
{
    return std::isfinite(a.x) && std::isfinite(a.y);
}

DimensionArrow arrowAt(Vec2 tip, Vec2 direction, double size)
{
    const Vec2 base = tip - (direction * size);
    const Vec2 side = leftOf(direction) * (std::tan(kDimensionArrowDegrees * std::numbers::pi / 180.0) * size);
    return {.tip = tip, .left = base + side, .right = base - side};
}

void addLine(DimensionLayout& layout, Vec2 from, Vec2 to)
{
    if (distance(from, to) > kMinLength) {
        layout.lines.push_back({.from = from, .to = to});
    }
}

struct Linear {
    Vec2 first;
    Vec2 second;
    double flyout = 0.0;
    std::optional<Vec2> direction;
    bool oneSide = false;
    bool fit = false;
};

struct Fitted {
    Place place = Place::Center;
    bool external = false;
};

Fitted fitLinear(const Linear& linear, Place place, double arrow)
{
    const double width = distance(linear.first, linear.second);
    const double arrows = linear.oneSide ? arrow : 2.0 * arrow;
    Fitted fitted{.place = place, .external = linear.fit && width < arrows};
    if (place == Place::Fit) {
        fitted.place = width < (fitted.external ? 0.0 : arrows) ? Place::Left : Place::Center;
    }
    return fitted;
}

Vec2 linearText(const Linear& linear, double arrow)
{
    const Vec2 d = unit(linear.second - linear.first);
    const Vec2 normal = leftOf(d);
    const Vec2 begin = linear.first + (normal * linear.flyout);
    const Vec2 end = linear.second + (normal * linear.flyout);
    const Fitted fitted = fitLinear(linear, linear.fit ? Place::Fit : Place::Center, arrow);
    const double offset = kDimensionExtension + (fitted.external ? arrow : 0.0);
    switch (fitted.place) {
    case Place::Left:
        return end - (d * offset);
    case Place::Right:
        return begin + (d * offset);
    case Place::Center:
    case Place::Fit:
        break;
    }
    return (begin + end) * 0.5;
}

void layOutLinear(DimensionLayout& layout, Linear linear, const std::optional<Vec2>& label, double arrow)
{
    const Vec2 d = unit(linear.second - linear.first);
    double side = 1.0;
    Place place = linear.fit ? Place::Fit : Place::Center;
    double extension = kDimensionExtension;
    if (label) {
        if (std::abs(cross(d, *label - linear.first)) > kOnLine) {
            side = cross(linear.first - *label, linear.second - *label) < 0.0 ? -1.0 : 1.0;
        }
        const Vec2 along = d * dot(*label - linear.first, d);
        const Vec2 away = *label - (linear.first + along);
        linear.flyout = dot(away, leftOf(d) * side);
        if (dot(unit(*label - linear.first), d) < 0.0) {
            place = Place::Left;
            extension = std::max(0.0, distance(*label, linear.first + away) - arrow);
        } else if (length(along) > distance(linear.first, linear.second)) {
            place = Place::Right;
            extension = std::max(0.0, distance(*label, linear.second + away) - arrow);
        } else {
            place = Place::Center;
        }
    }
    const Vec2 normal = leftOf(linear.direction.value_or(d)) * side;
    const double reach = linear.direction ? dot(linear.second - linear.first, normal) : 0.0;
    const Vec2 begin = linear.first + (normal * (linear.flyout + reach));
    const Vec2 end = linear.second + (normal * linear.flyout);
    const Fitted fitted = fitLinear(linear, place, arrow);
    const Vec2 lineDirection = unit(end - begin);
    const Vec2 firstArrow = lineDirection * (fitted.external ? 1.0 : -1.0);
    const Vec2 secondArrow = lineDirection * (fitted.external ? -1.0 : 1.0);
    const Vec2 firstEnd = begin - (firstArrow * arrow);
    const Vec2 secondEnd = end - (secondArrow * arrow);
    addLine(layout, fitted.external ? begin : firstEnd, fitted.external || linear.oneSide ? end : secondEnd);
    layout.arrows.push_back(arrowAt(begin, firstArrow, arrow));
    if (!linear.oneSide) {
        layout.arrows.push_back(arrowAt(end, secondArrow, arrow));
    }
    const bool firstTail = fitted.external && fitted.place != Place::Left;
    const bool secondTail = fitted.external && !linear.oneSide && fitted.place != Place::Right;
    if (fitted.place == Place::Left) {
        const Vec2 from = fitted.external ? firstEnd : begin;
        addLine(layout, from, from - (lineDirection * extension));
    } else if (fitted.place == Place::Right) {
        const Vec2 from = fitted.external ? secondEnd : end;
        addLine(layout, from, from + (lineDirection * extension));
    }
    if (firstTail) {
        addLine(layout, firstEnd, firstEnd - (lineDirection * kDimensionExtension));
    }
    if (secondTail) {
        addLine(layout, secondEnd, secondEnd + (lineDirection * kDimensionExtension));
    }
    addLine(layout, linear.first, begin);
    addLine(layout, linear.second, end);
}

std::optional<std::pair<Vec2, Vec2>> lineOf(const Sketch& sketch, Id id)
{
    const Curve* curve = sketch.curve(id);
    if (curve == nullptr || curve->kind != CurveKind::Line || curve->points.size() < 2) {
        return std::nullopt;
    }
    const Point* a = sketch.point(curve->points[0]);
    const Point* b = sketch.point(curve->points[1]);
    if (a == nullptr || b == nullptr) {
        return std::nullopt;
    }
    return std::pair{a->pos, b->pos};
}

std::optional<Vec2> rimAnchor(const Sketch& sketch, const Curve& curve)
{
    const Point* center = curve.points.empty() ? nullptr : sketch.point(curve.points[0]);
    if (center == nullptr) {
        return std::nullopt;
    }
    double angle = kHalfTurn / 4.0;
    if (curve.kind == CurveKind::Arc) {
        const Point* start = curve.points.size() > 2 ? sketch.point(curve.points[1]) : nullptr;
        const Point* end = curve.points.size() > 2 ? sketch.point(curve.points[2]) : nullptr;
        if (start == nullptr || end == nullptr) {
            return std::nullopt;
        }
        const Vec2 from = start->pos - center->pos;
        const Vec2 to = end->pos - center->pos;
        const double first = std::atan2(from.y, from.x);
        const double sweep = std::fmod(std::atan2(to.y, to.x) - first + (4.0 * kHalfTurn), 2.0 * kHalfTurn);
        angle = first + (sweep / 2.0);
    }
    return center->pos + (Vec2{std::cos(angle), std::sin(angle)} * curve.radius);
}

bool layOutRound(DimensionLayout& layout, const Sketch& sketch, const Constraint& constraint,
                 const std::optional<Vec2>& label, double arrow)
{
    const Curve* curve = constraint.refs.empty() ? nullptr : sketch.curve(constraint.refs[0]);
    if (curve == nullptr || curve->kind == CurveKind::Line || !(curve->radius > kMinLength)) {
        return false;
    }
    const Point* center = sketch.point(curve->points[0]);
    if (constraint.kind == ConstraintKind::Radius) {
        const std::optional<Vec2> anchor = rimAnchor(sketch, *curve);
        if (!anchor) {
            return false;
        }
        const Linear linear{.first = *anchor, .second = center->pos, .oneSide = true, .fit = true};
        layOutLinear(layout, linear, label, arrow);
        layout.text = label.value_or(linearText(linear, arrow));
        return true;
    }
    const Vec2 anchor = center->pos + Vec2{curve->radius, 0.0};
    layOutLinear(layout, {.first = anchor, .second = (center->pos * 2.0) - anchor, .fit = true}, label, arrow);
    layout.text = label.value_or(linearText({.first = anchor, .second = center->pos, .fit = true}, arrow));
    return true;
}

bool layOutAngle(DimensionLayout& layout, const Sketch& sketch, const Constraint& constraint,
                 const std::optional<Vec2>& label, double worldPerPixel, double arrow)
{
    if (constraint.refs.size() != 2) {
        return false;
    }
    const auto first = lineOf(sketch, constraint.refs[0]);
    const auto second = lineOf(sketch, constraint.refs[1]);
    if (!first || !second) {
        return false;
    }
    const Vec2 da = first->second - first->first;
    const Vec2 db = second->second - second->first;
    const double denominator = cross(da, db);
    if (std::abs(denominator) <= kParallelCross) {
        return false;
    }
    const Vec2 corner = first->first + (da * (cross(second->first - first->first, db) / denominator));
    const Vec2 far1 = distance(first->first, corner) > distance(first->second, corner) ? first->first : first->second;
    const Vec2 far3 =
        distance(second->first, corner) > distance(second->second, corner) ? second->first : second->second;
    const double reach = std::min(distance(far1, corner), distance(far3, corner));
    if (!(reach > kMinLength)) {
        return false;
    }
    const Vec2 u1 = unit(far1 - corner);
    const Vec2 u2 = unit(far3 - corner);
    const double sense = cross(u1, u2) < 0.0 ? -1.0 : 1.0;
    const double opening = std::acos(std::clamp(dot(u1, u2), -1.0, 1.0));
    const Vec2 bisector = unit(u1 + u2);
    double flyout = std::min(kAngleDimensionPixels * worldPerPixel, kAngleDimensionLegShare * reach);
    Place place = Place::Fit;
    double extension = kDimensionExtension;
    if (label) {
        const Vec2 toLabel = *label - corner;
        const double radius = length(toLabel);
        const double inside = std::cos(opening / 2.0);
        place = Place::Center;
        if (dot(unit(toLabel), bisector) > inside) {
            flyout = radius;
        } else if (dot(unit(toLabel), bisector * -1.0) > inside) {
            flyout = -radius;
        } else {
            const double off1 = std::abs(cross(toLabel, u1));
            const double off2 = std::abs(cross(toLabel, u2));
            const bool nearFirst = off1 <= off2;
            const Vec2 leg = nearFirst ? u1 : u2;
            const double along = dot(toLabel, leg);
            flyout = along >= 0.0 ? std::abs(along) : -std::abs(along);
            extension = std::max(0.0, (nearFirst ? off1 : off2) - arrow);
            place = nearFirst ? Place::Left : Place::Right;
        }
    }
    const Vec2 attach1 = corner + (u1 * flyout);
    const Vec2 attach2 = corner + (u2 * flyout);
    const bool external = distance(attach1, attach2) < 2.0 * arrow;
    if (place == Place::Fit) {
        place = distance(attach1, attach2) < (external ? 0.0 : 2.0 * arrow) ? Place::Left : Place::Center;
    }
    const Vec2 out1 = leftOf(unit(attach1 - corner)) * -sense;
    const Vec2 out2 = leftOf(unit(attach2 - corner)) * sense;
    const Vec2 arrow1 = out1 * (external ? -1.0 : 1.0);
    const Vec2 arrow2 = out2 * (external ? -1.0 : 1.0);
    const Vec2 end1 = attach1 - (arrow1 * arrow);
    const Vec2 end2 = attach2 - (arrow2 * arrow);
    const Vec2 from = (external ? attach1 : end1) - corner;
    const Vec2 to = (external ? attach2 : end2) - corner;
    const double start = std::atan2(from.y, from.x);
    double sweep = sense * (std::atan2(to.y, to.x) - start);
    sweep = std::fmod(sweep + (4.0 * kHalfTurn), 2.0 * kHalfTurn);
    const double turned = std::acos(std::clamp(dot(unit(attach1 - corner), unit(attach2 - corner)), -1.0, 1.0));
    const int points = std::max(kMinArcPoints, static_cast<int>(kArcPointsPerHalfTurn * turned / kHalfTurn));
    const double radius = std::abs(flyout);
    for (int i = 0; i < points; ++i) {
        const double angle = start + (sense * sweep * i / (points - 1));
        layout.arc.push_back(corner + (Vec2{std::cos(angle), std::sin(angle)} * radius));
    }
    layout.arrows.push_back(arrowAt(attach1, arrow1, arrow));
    layout.arrows.push_back(arrowAt(attach2, arrow2, arrow));
    if (place == Place::Left) {
        const Vec2 start1 = external ? end1 : attach1;
        addLine(layout, start1, start1 + (out1 * extension));
    } else if (place == Place::Right) {
        const Vec2 start2 = external ? end2 : attach2;
        addLine(layout, start2, start2 + (out2 * extension));
    }
    if (external && place != Place::Left) {
        addLine(layout, end1, end1 + (out1 * kDimensionExtension));
    }
    if (external && place != Place::Right) {
        addLine(layout, end2, end2 + (out2 * kDimensionExtension));
    }
    addLine(layout, corner, attach1);
    addLine(layout, corner, attach2);
    layout.text = label.value_or(corner + (bisector * flyout));
    return true;
}

} // namespace

std::string dimensionLabel(ConstraintKind kind, double value)
{
    std::string text = std::format("{:.{}f}", value, kLabelDecimals);
    while (text.ends_with('0')) {
        text.pop_back();
    }
    if (text.ends_with('.')) {
        text.pop_back();
    }
    switch (kind) {
    case ConstraintKind::Radius:
        return kRadiusPrefix + text;
    case ConstraintKind::Diameter:
        return kDiameterPrefix + text;
    case ConstraintKind::Angle:
        return text + kDegreeSuffix;
    case ConstraintKind::Coincident:
    case ConstraintKind::Horizontal:
    case ConstraintKind::Vertical:
    case ConstraintKind::Parallel:
    case ConstraintKind::Perpendicular:
    case ConstraintKind::Tangent:
    case ConstraintKind::Equal:
    case ConstraintKind::PointOnCurve:
    case ConstraintKind::Midpoint:
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
    case ConstraintKind::Symmetric:
    case ConstraintKind::Offset:
        break;
    }
    return text;
}

Vec2 dimensionMiddle(const Sketch& sketch)
{
    Vec2 lowest{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec2 highest{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
    for (const auto& [id, point] : sketch.points()) {
        lowest = {std::min(lowest.x, point.pos.x), std::min(lowest.y, point.pos.y)};
        highest = {std::max(highest.x, point.pos.x), std::max(highest.y, point.pos.y)};
    }
    return (lowest + highest) * 0.5;
}

std::optional<DimensionLayout> layoutDimension(const Sketch& sketch, const Constraint& constraint, double worldPerPixel,
                                               Vec2 middle)
{
    if (!std::isfinite(worldPerPixel) || !(worldPerPixel > 0.0) || !isDimension(constraint.kind)) {
        return std::nullopt;
    }
    const double arrow = kDimensionArrowPixels * worldPerPixel;
    const std::optional<Vec2> label = labelPosition(sketch, constraint);
    DimensionLayout layout{
        .lines = {}, .arc = {}, .arrows = {}, .text = {}, .label = dimensionLabel(constraint.kind, constraint.value)};
    bool laid = false;
    switch (constraint.kind) {
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
    case ConstraintKind::Offset: {
        const auto ends = measuredPoints(sketch, constraint);
        if (!ends || !(distance(ends->first, ends->second) > kMinLength)) {
            break;
        }
        const auto [a, b] = *ends;
        Vec2 measured = b - a;
        std::optional<Vec2> direction;
        if (constraint.kind == ConstraintKind::HorizontalDistance) {
            measured = {b.x - a.x, 0.0};
            direction = Vec2{1.0, 0.0};
        } else if (constraint.kind == ConstraintKind::VerticalDistance) {
            measured = {0.0, b.y - a.y};
            direction = Vec2{0.0, 1.0};
        }
        const double sign = dot(leftOf(measured), ((a + b) * 0.5) - middle) < 0.0 ? -1.0 : 1.0;
        const Linear linear{
            .first = a, .second = b, .flyout = sign * worldPerPixel * kDimensionFlyoutPixels, .direction = direction};
        layOutLinear(layout, linear, label, arrow);
        layout.text = label.value_or(linearText(linear, arrow));
        laid = true;
        break;
    }
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter:
        laid = layOutRound(layout, sketch, constraint, label, arrow);
        break;
    case ConstraintKind::Angle:
        laid = layOutAngle(layout, sketch, constraint, label, worldPerPixel, arrow);
        break;
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
        break;
    }
    if (!laid || !finite(layout.text)) {
        return std::nullopt;
    }
    return layout;
}

std::map<Id, DimensionLayout> layoutDimensions(const Sketch& sketch, double worldPerPixel)
{
    std::map<Id, DimensionLayout> layouts;
    const Vec2 middle = dimensionMiddle(sketch);
    for (const auto& [id, constraint] : sketch.constraints()) {
        if (std::optional<DimensionLayout> layout = layoutDimension(sketch, constraint, worldPerPixel, middle)) {
            layouts.emplace(id, std::move(*layout));
        }
    }
    return layouts;
}

} // namespace kalve137::sketch
