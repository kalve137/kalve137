// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalve137::sketch {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}
inline Vec2 operator-(Vec2 a, Vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}
inline Vec2 operator*(Vec2 a, double s)
{
    return {a.x * s, a.y * s};
}

double distance(Vec2 a, Vec2 b);

using Id = int;

inline constexpr Id kOriginId = 1;

struct Point {
    Vec2 pos;
    bool fixed = false;
};

enum class CurveKind {
    Line,
    Circle,
    Arc,
};

struct Curve {
    CurveKind kind = CurveKind::Line;
    std::vector<Id> points;
    double radius = 0.0;
    bool construction = false;
    bool fixed = false;
};

enum class ConstraintKind {
    Coincident,
    Horizontal,
    Vertical,
    Parallel,
    Perpendicular,
    Tangent,
    Equal,
    PointOnCurve,
    Midpoint,
    Distance,
    HorizontalDistance,
    VerticalDistance,
    Radius,
    Diameter,
    Angle,
    Symmetric,
};

std::string_view toString(ConstraintKind kind);
std::optional<ConstraintKind> constraintKindFromString(std::string_view name);
std::string_view toString(CurveKind kind);
std::optional<CurveKind> curveKindFromString(std::string_view name);

bool isDimension(ConstraintKind kind);

struct Constraint {
    ConstraintKind kind = ConstraintKind::Coincident;
    std::vector<Id> refs;
    double value = 0.0;
    std::optional<Vec2> labelOffset;
};

class Sketch {
public:
    Sketch();

    Id addPoint(Vec2 pos, bool fixed = false);
    Id addLine(Id start, Id end, bool construction = false);
    Id addCircle(Id center, double radius, bool construction = false);
    Id addArc(Id center, Id start, Id end, bool construction = false);
    Id addConstraint(ConstraintKind kind, std::vector<Id> refs, double value = 0.0);

    void remove(Id id);

    void insertPoint(Id id, Point point);
    void insertCurve(Id id, Curve curve);
    void insertConstraint(Id id, Constraint constraint);

    const std::map<Id, Point>& points() const { return m_points; }
    const std::map<Id, Curve>& curves() const { return m_curves; }
    const std::map<Id, Constraint>& constraints() const { return m_constraints; }

    const Point* point(Id id) const;
    Point* point(Id id);
    const Curve* curve(Id id) const;
    Curve* curve(Id id);
    const Constraint* constraint(Id id) const;
    Constraint* constraint(Id id);

    bool isPoint(Id id) const { return m_points.contains(id); }
    bool isCurve(Id id) const { return m_curves.contains(id); }

    std::vector<Id> curvesUsing(Id point) const;
    std::optional<Vec2> midpoint(Id line) const;

    Id nextId() const { return m_nextId; }

private:
    Id takeId();
    void dropUnusedPoints(const std::vector<Id>& candidates);

    std::map<Id, Point> m_points;
    std::map<Id, Curve> m_curves;
    std::map<Id, Constraint> m_constraints;
    Id m_nextId = kOriginId + 1;
};

std::optional<std::string> validate(const Sketch& sketch);

} // namespace kalve137::sketch
