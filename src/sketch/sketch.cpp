// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/sketch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace kalve137::sketch {

namespace {

constexpr std::array kConstraintNames = {
    std::pair{ConstraintKind::Coincident, std::string_view("coincident")},
    std::pair{ConstraintKind::Horizontal, std::string_view("horizontal")},
    std::pair{ConstraintKind::Vertical, std::string_view("vertical")},
    std::pair{ConstraintKind::Parallel, std::string_view("parallel")},
    std::pair{ConstraintKind::Perpendicular, std::string_view("perpendicular")},
    std::pair{ConstraintKind::Tangent, std::string_view("tangent")},
    std::pair{ConstraintKind::Equal, std::string_view("equal")},
    std::pair{ConstraintKind::PointOnCurve, std::string_view("point_on_curve")},
    std::pair{ConstraintKind::Midpoint, std::string_view("midpoint")},
    std::pair{ConstraintKind::Distance, std::string_view("distance")},
    std::pair{ConstraintKind::HorizontalDistance, std::string_view("horizontal_distance")},
    std::pair{ConstraintKind::VerticalDistance, std::string_view("vertical_distance")},
    std::pair{ConstraintKind::Radius, std::string_view("radius")},
    std::pair{ConstraintKind::Diameter, std::string_view("diameter")},
    std::pair{ConstraintKind::Angle, std::string_view("angle")},
    std::pair{ConstraintKind::Symmetric, std::string_view("symmetric")},
};

constexpr std::array kCurveNames = {
    std::pair{CurveKind::Line, std::string_view("line")},
    std::pair{CurveKind::Circle, std::string_view("circle")},
    std::pair{CurveKind::Arc, std::string_view("arc")},
};

template <typename Table, typename Kind>
std::string_view nameOf(const Table& table, Kind kind)
{
    for (const auto& [k, name] : table) {
        if (k == kind) {
            return name;
        }
    }
    return {};
}

template <typename Kind, typename Table>
std::optional<Kind> kindOf(const Table& table, std::string_view name)
{
    for (const auto& [k, n] : table) {
        if (n == name) {
            return k;
        }
    }
    return std::nullopt;
}

} // namespace

double distance(Vec2 a, Vec2 b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

std::string_view toString(ConstraintKind kind)
{
    return nameOf(kConstraintNames, kind);
}

std::optional<ConstraintKind> constraintKindFromString(std::string_view name)
{
    return kindOf<ConstraintKind>(kConstraintNames, name);
}

std::string_view toString(CurveKind kind)
{
    return nameOf(kCurveNames, kind);
}

std::optional<CurveKind> curveKindFromString(std::string_view name)
{
    return kindOf<CurveKind>(kCurveNames, name);
}

bool isDimension(ConstraintKind kind)
{
    switch (kind) {
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter:
    case ConstraintKind::Angle:
        return true;
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
        return false;
    }
    return false;
}

Sketch::Sketch()
{
    m_points.emplace(kOriginId, Point{{0.0, 0.0}, true});
}

Id Sketch::takeId()
{
    return m_nextId++;
}

Id Sketch::addPoint(Vec2 pos, bool fixed)
{
    const Id id = takeId();
    m_points.emplace(id, Point{pos, fixed});
    return id;
}

Id Sketch::addLine(Id start, Id end, bool construction)
{
    const Id id = takeId();
    m_curves.emplace(id, Curve{CurveKind::Line, {start, end}, 0.0, construction, false});
    return id;
}

Id Sketch::addCircle(Id center, double radius, bool construction)
{
    const Id id = takeId();
    m_curves.emplace(id, Curve{CurveKind::Circle, {center}, radius, construction, false});
    return id;
}

Id Sketch::addArc(Id center, Id start, Id end, bool construction)
{
    const Id id = takeId();
    const double radius = distance(m_points.at(center).pos, m_points.at(start).pos);
    m_curves.emplace(id, Curve{CurveKind::Arc, {center, start, end}, radius, construction, false});
    return id;
}

Id Sketch::addConstraint(ConstraintKind kind, std::vector<Id> refs, double value)
{
    const Id id = takeId();
    m_constraints.emplace(id, Constraint{kind, std::move(refs), value});
    return id;
}

void Sketch::insertPoint(Id id, Point point)
{
    m_points.insert_or_assign(id, point);
    m_nextId = std::max(m_nextId, id + 1);
}

void Sketch::insertCurve(Id id, Curve curve)
{
    m_curves.insert_or_assign(id, std::move(curve));
    m_nextId = std::max(m_nextId, id + 1);
}

void Sketch::insertConstraint(Id id, Constraint constraint)
{
    m_constraints.insert_or_assign(id, std::move(constraint));
    m_nextId = std::max(m_nextId, id + 1);
}

void Sketch::remove(Id id)
{
    if (id == kOriginId) {
        return;
    }
    if (m_constraints.erase(id) > 0) {
        return;
    }

    std::vector<Id> removedCurves;
    std::vector<Id> orphanCandidates;
    if (auto it = m_curves.find(id); it != m_curves.end()) {
        orphanCandidates = it->second.points;
        removedCurves.push_back(id);
        m_curves.erase(it);
    } else if (m_points.erase(id) > 0) {
        for (auto curveIt = m_curves.begin(); curveIt != m_curves.end();) {
            const std::vector<Id>& used = curveIt->second.points;
            if (std::ranges::find(used, id) != used.end()) {
                orphanCandidates.insert(orphanCandidates.end(), used.begin(), used.end());
                removedCurves.push_back(curveIt->first);
                curveIt = m_curves.erase(curveIt);
            } else {
                ++curveIt;
            }
        }
        removedCurves.push_back(id);
    } else {
        return;
    }

    std::erase_if(m_constraints, [&](const auto& entry) {
        return std::ranges::any_of(
            entry.second.refs, [&](Id ref) { return std::ranges::find(removedCurves, ref) != removedCurves.end(); });
    });
    dropUnusedPoints(orphanCandidates);
}

void Sketch::dropUnusedPoints(const std::vector<Id>& candidates)
{
    for (Id point : candidates) {
        if (point == kOriginId || !m_points.contains(point) || !curvesUsing(point).empty()) {
            continue;
        }
        m_points.erase(point);
        std::erase_if(m_constraints, [&](const auto& entry) {
            return std::ranges::find(entry.second.refs, point) != entry.second.refs.end();
        });
    }
}

const Point* Sketch::point(Id id) const
{
    auto it = m_points.find(id);
    return it == m_points.end() ? nullptr : &it->second;
}

Point* Sketch::point(Id id)
{
    auto it = m_points.find(id);
    return it == m_points.end() ? nullptr : &it->second;
}

const Curve* Sketch::curve(Id id) const
{
    auto it = m_curves.find(id);
    return it == m_curves.end() ? nullptr : &it->second;
}

Curve* Sketch::curve(Id id)
{
    auto it = m_curves.find(id);
    return it == m_curves.end() ? nullptr : &it->second;
}

const Constraint* Sketch::constraint(Id id) const
{
    auto it = m_constraints.find(id);
    return it == m_constraints.end() ? nullptr : &it->second;
}

Constraint* Sketch::constraint(Id id)
{
    auto it = m_constraints.find(id);
    return it == m_constraints.end() ? nullptr : &it->second;
}

std::vector<Id> Sketch::curvesUsing(Id point) const
{
    std::vector<Id> result;
    for (const auto& [id, curve] : m_curves) {
        if (std::ranges::find(curve.points, point) != curve.points.end()) {
            result.push_back(id);
        }
    }
    return result;
}

std::optional<Vec2> Sketch::midpoint(Id line) const
{
    const Curve* c = curve(line);
    if (c == nullptr || c->kind != CurveKind::Line) {
        return std::nullopt;
    }
    const Vec2 a = m_points.at(c->points[0]).pos;
    const Vec2 b = m_points.at(c->points[1]).pos;
    return Vec2{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

std::optional<std::string> validate(const Sketch& sketch)
{
    if (!sketch.isPoint(kOriginId)) {
        return "the origin point is missing";
    }
    for (const auto& [id, point] : sketch.points()) {
        if (!std::isfinite(point.pos.x) || !std::isfinite(point.pos.y)) {
            return std::format("point {} has no valid position", id);
        }
    }
    for (const auto& [id, curve] : sketch.curves()) {
        if (sketch.isPoint(id)) {
            return std::format("id {} is both a point and a curve", id);
        }
        const std::size_t needed = [kind = curve.kind]() -> std::size_t {
            switch (kind) {
            case CurveKind::Line:
                return 2;
            case CurveKind::Circle:
                return 1;
            case CurveKind::Arc:
                return 3;
            }
            return 0;
        }();
        if (curve.points.size() != needed) {
            return std::format("{} {} has {} points instead of {}", toString(curve.kind), id, curve.points.size(),
                               needed);
        }
        for (const Id point : curve.points) {
            if (!sketch.isPoint(point)) {
                return std::format("{} {} uses missing point {}", toString(curve.kind), id, point);
            }
        }
        if (!std::isfinite(curve.radius) || curve.radius < 0.0) {
            return std::format("{} {} has no valid radius", toString(curve.kind), id);
        }
    }
    for (const auto& [id, constraint] : sketch.constraints()) {
        if (sketch.isPoint(id) || sketch.isCurve(id)) {
            return std::format("id {} is used twice", id);
        }
        for (const Id ref : constraint.refs) {
            if (!sketch.isPoint(ref) && !sketch.isCurve(ref)) {
                return std::format("{} constraint {} refers to missing {}", toString(constraint.kind), id, ref);
            }
        }
        if (!std::isfinite(constraint.value)) {
            return std::format("{} constraint {} has no valid value", toString(constraint.kind), id);
        }
    }
    return std::nullopt;
}

} // namespace kalve137::sketch
