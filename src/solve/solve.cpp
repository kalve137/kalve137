// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "solve/solve.h"

#include <planegcs/GCS.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kalve137::solve {

namespace {

using Kind = Constraint::Kind;
using RefKind = Ref::Kind;

class Model {
public:
    explicit Model(const Sketch& sketch) : m_sketch(sketch)
    {
        m_params.reserve(2 * sketch.points.size() + sketch.circles.size() + 3 * sketch.arcs.size() +
                         sketch.constraints.size());
        for (const Point& p : sketch.points) {
            GCS::Point point;
            point.x = push(p.u);
            point.y = push(p.v);
            m_points.push_back(point);
            m_unknowns.push_back(point.x);
            m_unknowns.push_back(point.y);
        }
        for (const Line& l : sketch.lines) {
            GCS::Line line;
            line.p1 = point(l.a);
            line.p2 = point(l.b);
            m_lines.push_back(line);
        }
        for (const Circle& c : sketch.circles) {
            GCS::Circle circle;
            circle.center = point(c.center);
            circle.rad = push(c.radius);
            m_circles.push_back(circle);
            m_unknowns.push_back(circle.rad);
        }
        for (const Arc& a : sketch.arcs) {
            const Point& c = at(a.center);
            const Point& s = at(a.start);
            const Point& e = at(a.end);
            const double start = std::atan2(s.v - c.v, s.u - c.u);
            double end = std::atan2(e.v - c.v, e.u - c.u);
            if (end <= start) {
                end += 2.0 * std::numbers::pi;
            }
            GCS::Arc arc;
            arc.center = point(a.center);
            arc.start = point(a.start);
            arc.end = point(a.end);
            arc.rad = push(std::hypot(s.u - c.u, s.v - c.v));
            arc.startAngle = push(start);
            arc.endAngle = push(end);
            m_arcs.push_back(arc);
            m_unknowns.push_back(arc.rad);
            m_unknowns.push_back(arc.startAngle);
            m_unknowns.push_back(arc.endAngle);
        }
    }

    void addConstraints(GCS::System& system, int skip = -1)
    {
        for (std::size_t i = 0; i < m_sketch.constraints.size(); ++i) {
            if (static_cast<int>(i) != skip) {
                add(system, static_cast<int>(i) + 1, m_sketch.constraints[i]);
            }
        }
        for (GCS::Arc& arc : m_arcs) {
            system.addConstraintArcRules(arc, 0);
        }
    }

    GCS::VEC_pD& unknowns() { return m_unknowns; }

    void read(Solution& out) const
    {
        for (std::size_t i = 0; i < m_points.size(); ++i) {
            out.points.push_back({*m_points[i].x, *m_points[i].y});
        }
        for (const GCS::Circle& circle : m_circles) {
            out.circleRadii.push_back(*circle.rad);
        }
        for (const GCS::Arc& arc : m_arcs) {
            out.arcRadii.push_back(*arc.rad);
        }
    }

    bool finite() const
    {
        return std::ranges::all_of(m_params, [](double value) { return std::isfinite(value); });
    }

    void free(const GCS::VEC_pD& dependent, Solution& out) const
    {
        const std::unordered_set<const double*> loose(dependent.begin(), dependent.end());
        for (std::size_t i = 0; i < m_points.size(); ++i) {
            if (loose.contains(m_points[i].x) || loose.contains(m_points[i].y)) {
                out.freePoints.push_back(static_cast<int>(i));
            }
        }
        for (std::size_t i = 0; i < m_circles.size(); ++i) {
            if (loose.contains(m_circles[i].rad)) {
                out.freeCircles.push_back(static_cast<int>(i));
            }
        }
    }

private:
    double* push(double value)
    {
        if (m_params.size() == m_params.capacity()) {
            throw std::logic_error("A sketch needs more solver values than were reserved for it");
        }
        m_params.push_back(value);
        return &m_params.back();
    }

    const Point& at(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_sketch.points.size())) {
            throw std::invalid_argument("The sketch refers to point " + std::to_string(index) + ", which it lacks");
        }
        return m_sketch.points[static_cast<std::size_t>(index)];
    }

    GCS::Point& point(int index)
    {
        at(index);
        return m_points[static_cast<std::size_t>(index)];
    }

    template <typename T>
    static T* pick(std::vector<T>& items, const Constraint& c, std::size_t slot, RefKind kind)
    {
        if (slot >= c.refs.size() || c.refs[slot].kind != kind) {
            return nullptr;
        }
        const int index = c.refs[slot].index;
        if (index < 0 || index >= static_cast<int>(items.size())) {
            throw std::invalid_argument("A constraint refers to an entity the sketch lacks");
        }
        return &items[static_cast<std::size_t>(index)];
    }

    GCS::Curve* curve(const Constraint& c, std::size_t slot)
    {
        if (GCS::Line* line = pick(m_lines, c, slot, RefKind::Line)) {
            return line;
        }
        if (GCS::Arc* arc = pick(m_arcs, c, slot, RefKind::Arc)) {
            return arc;
        }
        return pick(m_circles, c, slot, RefKind::Circle);
    }

    std::vector<int> ends(const Ref& ref) const
    {
        if (ref.kind == RefKind::Line) {
            const Line& line = m_sketch.lines.at(static_cast<std::size_t>(ref.index));
            return {line.a, line.b};
        }
        if (ref.kind == RefKind::Arc) {
            const Arc& arc = m_sketch.arcs.at(static_cast<std::size_t>(ref.index));
            return {arc.start, arc.end};
        }
        return {};
    }

    void add(GCS::System& sys, int tag, const Constraint& c)
    {
        GCS::Point* p0 = pick(m_points, c, 0, RefKind::Point);
        GCS::Point* p1 = pick(m_points, c, 1, RefKind::Point);
        GCS::Line* l0 = pick(m_lines, c, 0, RefKind::Line);
        GCS::Line* l1 = pick(m_lines, c, 1, RefKind::Line);
        GCS::Circle* c0 = pick(m_circles, c, 0, RefKind::Circle);
        GCS::Circle* c1 = pick(m_circles, c, 1, RefKind::Circle);
        GCS::Arc* a0 = pick(m_arcs, c, 0, RefKind::Arc);
        GCS::Arc* a1 = pick(m_arcs, c, 1, RefKind::Arc);
        const std::size_t count = c.refs.size();
        bool added = true;
        switch (c.kind) {
        case Kind::X:
            added = p0 && count == 1;
            if (added) {
                sys.addConstraintCoordinateX(*p0, push(c.value), tag);
            }
            break;
        case Kind::Y:
            added = p0 && count == 1;
            if (added) {
                sys.addConstraintCoordinateY(*p0, push(c.value), tag);
            }
            break;
        case Kind::Coincident:
            if (p0 && p1) {
                sys.addConstraintP2PCoincident(*p0, *p1, tag);
            } else if (p0 && l1) {
                sys.addConstraintPointOnLine(*p0, *l1, tag);
            } else if (p0 && c1) {
                sys.addConstraintPointOnCircle(*p0, *c1, tag);
            } else if (p0 && a1) {
                sys.addConstraintPointOnArc(*p0, *a1, tag);
            } else {
                added = false;
            }
            break;
        case Kind::Horizontal:
            if (l0 && count == 1) {
                sys.addConstraintHorizontal(*l0, tag);
            } else if (p0 && p1) {
                sys.addConstraintHorizontal(*p0, *p1, tag);
            } else {
                added = false;
            }
            break;
        case Kind::Vertical:
            if (l0 && count == 1) {
                sys.addConstraintVertical(*l0, tag);
            } else if (p0 && p1) {
                sys.addConstraintVertical(*p0, *p1, tag);
            } else {
                added = false;
            }
            break;
        case Kind::Parallel:
            added = l0 && l1;
            if (added) {
                sys.addConstraintParallel(*l0, *l1, tag);
            }
            break;
        case Kind::Perpendicular:
            added = l0 && l1;
            if (added) {
                sys.addConstraintPerpendicular(*l0, *l1, tag);
            }
            break;
        case Kind::Equal:
            if (l0 && l1) {
                sys.addConstraintEqualLength(*l0, *l1, tag);
            } else if (c0 && c1) {
                sys.addConstraintEqualRadius(*c0, *c1, tag);
            } else if (c0 && a1) {
                sys.addConstraintEqualRadius(*c0, *a1, tag);
            } else if (a0 && c1) {
                sys.addConstraintEqualRadius(*c1, *a0, tag);
            } else if (a0 && a1) {
                sys.addConstraintEqualRadius(*a0, *a1, tag);
            } else {
                added = false;
            }
            break;
        case Kind::Tangent:
            added = tangent(sys, tag, c);
            break;
        case Kind::Distance:
            if (p0 && p1) {
                sys.addConstraintP2PDistance(*p0, *p1, push(c.value), tag);
            } else if (p0 && l1) {
                sys.addConstraintP2LDistance(*p0, *l1, push(c.value), tag);
            } else if (l0 && count == 1) {
                sys.addConstraintP2PDistance(l0->p1, l0->p2, push(c.value), tag);
            } else {
                added = false;
            }
            break;
        case Kind::Radius:
            if (c0) {
                sys.addConstraintCircleRadius(*c0, push(c.value), tag);
            } else if (a0) {
                sys.addConstraintArcRadius(*a0, push(c.value), tag);
            } else {
                added = false;
            }
            break;
        case Kind::Angle:
            if (l0 && l1) {
                const double d0x = *l0->p2.x - *l0->p1.x;
                const double d0y = *l0->p2.y - *l0->p1.y;
                const double d1x = *l1->p2.x - *l1->p1.x;
                const double d1y = *l1->p2.y - *l1->p1.y;
                const double sign = d0x * d1y - d0y * d1x >= 0.0 ? 1.0 : -1.0;
                sys.addConstraintL2LAngle(*l0, *l1, push(sign * c.value), tag);
            } else {
                added = false;
            }
            break;
        }
        if (!added) {
            throw std::invalid_argument("Constraint " + std::to_string(tag - 1) + " has the wrong kind of entities");
        }
    }

    bool tangent(GCS::System& sys, int tag, const Constraint& c)
    {
        if (c.refs.size() != 2 || c.refs[0].kind == RefKind::Point || c.refs[1].kind == RefKind::Point) {
            return false;
        }
        const std::vector<int> first = ends(c.refs[0]);
        const std::vector<int> second = ends(c.refs[1]);
        for (const int end : first) {
            if (std::ranges::find(second, end) != second.end()) {
                GCS::Curve* a = curve(c, 0);
                GCS::Curve* b = curve(c, 1);
                GCS::Point& p = m_points[static_cast<std::size_t>(end)];
                const double angle = sys.calculateAngleViaPoint(*a, *b, p);
                const bool reversed = std::abs(std::remainder(angle, 2.0 * std::numbers::pi)) > std::numbers::pi / 2;
                sys.addConstraintAngleViaPoint(*a, *b, p, push(reversed ? std::numbers::pi : 0.0), tag);
                return true;
            }
        }
        std::size_t i = 0;
        std::size_t j = 1;
        if (c.refs[1].kind == RefKind::Line) {
            std::swap(i, j);
        }
        if (GCS::Line* line = pick(m_lines, c, i, RefKind::Line)) {
            if (GCS::Circle* circle = pick(m_circles, c, j, RefKind::Circle)) {
                sys.addConstraintTangent(*line, *circle, tag);
                return true;
            }
            if (GCS::Arc* arc = pick(m_arcs, c, j, RefKind::Arc)) {
                sys.addConstraintTangent(*line, *arc, tag);
                return true;
            }
            return false;
        }
        if (c.refs[0].kind == RefKind::Arc && c.refs[1].kind == RefKind::Circle) {
            std::swap(i, j);
        }
        if (GCS::Circle* circle = pick(m_circles, c, i, RefKind::Circle)) {
            if (GCS::Circle* other = pick(m_circles, c, j, RefKind::Circle)) {
                sys.addConstraintTangent(*circle, *other, tag);
                return true;
            }
            if (GCS::Arc* arc = pick(m_arcs, c, j, RefKind::Arc)) {
                sys.addConstraintTangent(*circle, *arc, tag);
                return true;
            }
            return false;
        }
        GCS::Arc* arc = pick(m_arcs, c, i, RefKind::Arc);
        GCS::Arc* other = pick(m_arcs, c, j, RefKind::Arc);
        if (arc && other) {
            sys.addConstraintTangent(*arc, *other, tag);
            return true;
        }
        return false;
    }

    const Sketch& m_sketch;
    std::vector<double> m_params;
    std::vector<GCS::Point> m_points;
    std::vector<GCS::Line> m_lines;
    std::vector<GCS::Circle> m_circles;
    std::vector<GCS::Arc> m_arcs;
    GCS::VEC_pD m_unknowns;
};

std::vector<int> indices(const GCS::VEC_I& tags)
{
    std::vector<int> out;
    for (const int tag : tags) {
        if (tag > 0) {
            out.push_back(tag - 1);
        }
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

class Run {
public:
    explicit Run(const Sketch& sketch, int skip = -1) : m_model(sketch)
    {
        m_model.addConstraints(m_system, skip);
        m_system.declareUnknowns(m_model.unknowns());
        m_system.initSolution();
    }

    int freedom() const { return std::max(m_system.dofsNumber(), 0); }

    std::vector<int> group() const
    {
        GCS::VEC_I tags;
        m_system.getConflicting(tags);
        std::vector<int> out = indices(tags);
        tags.clear();
        m_system.getRedundant(tags);
        for (const int i : indices(tags)) {
            if (std::ranges::find(out, i) == out.end()) {
                out.push_back(i);
            }
        }
        std::ranges::sort(out);
        return out;
    }

    bool solve()
    {
        bool solved = m_system.solve(true) == GCS::Success;
        if (solved) {
            m_system.applySolution();
            solved = m_model.finite();
        }
        if (!solved) {
            m_system.undoSolution();
        }
        return solved;
    }

    void read(Solution& out) const { m_model.read(out); }

    void free(Solution& out)
    {
        GCS::VEC_pD dependent;
        m_system.getDependentParams(dependent);
        if (dependent.empty()) {
            dependent = m_model.unknowns();
        }
        m_model.free(dependent, out);
    }

private:
    Model m_model;
    GCS::System m_system;
};

Sketch settled(const Sketch& sketch, const Solution& solution)
{
    Sketch out = sketch;
    for (std::size_t i = 0; i < out.points.size(); ++i) {
        out.points[i].u = solution.points[i].u;
        out.points[i].v = solution.points[i].v;
    }
    for (std::size_t i = 0; i < out.circles.size(); ++i) {
        out.circles[i].radius = solution.circleRadii[i];
    }
    return out;
}

} // namespace

Solution solve(const Sketch& sketch)
{
    Solution out;
    Run written(sketch);
    const bool solved = written.solve();
    written.read(out);
    if (!solved) {
        const std::vector<int> group = written.group();
        if (group.empty()) {
            out.status = Solution::Status::Failed;
            return out;
        }
        for (const int i : group) {
            Run trial(sketch, i);
            if (trial.group().empty() && trial.solve()) {
                out.conflicting.push_back(i);
            }
        }
        out.status = Solution::Status::Conflicting;
        out.conflicting = out.conflicting.empty() ? group : out.conflicting;
        return out;
    }
    const Sketch at = settled(sketch, out);
    Run there(at);
    out.freedom = there.freedom();
    const std::vector<int> group = there.group();
    for (int i = 0; !group.empty() && i < static_cast<int>(sketch.constraints.size()); ++i) {
        if (Run(at, i).freedom() <= out.freedom) {
            out.redundant.push_back(i);
        }
    }
    if (!group.empty()) {
        out.status = Solution::Status::Redundant;
        out.redundant = out.redundant.empty() ? group : out.redundant;
    } else {
        out.status = Solution::Status::Solved;
    }
    if (out.freedom > 0) {
        there.free(out);
    }
    return out;
}

std::optional<std::vector<Point>> positions(const Sketch& sketch)
{
    Run run(sketch);
    if (!run.solve()) {
        return std::nullopt;
    }
    Solution out;
    run.read(out);
    return out.points;
}

} // namespace kalve137::solve
