// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/solver.h"

#include "log/log.h"
#include <planegcs/GCS.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <unordered_set>
#include <vector>

namespace kalve137::sketch {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;

class SolverModel {
public:
    explicit SolverModel(const Sketch& sketch) : m_sketch(sketch)
    {
        std::size_t count = 2 * sketch.points().size();
        for (const auto& [id, curve] : sketch.curves()) {
            count += curve.kind == CurveKind::Arc ? 3 : (curve.kind == CurveKind::Circle ? 1 : 0);
        }
        count += 2 * sketch.constraints().size();
        m_params.reserve(count);

        for (const auto& [id, point] : sketch.points()) {
            const std::size_t index = push(point.pos.x);
            push(point.pos.y);
            m_pointIndex.emplace(id, index);
            m_points.emplace(id, GCS::Point(&m_params[index], &m_params[index + 1]));
        }
        for (const auto& [id, curve] : sketch.curves()) {
            buildCurve(id, curve);
        }
        collectUnknowns();
    }

    void addConstraints(GCS::System& system)
    {
        for (const auto& [id, constraint] : m_sketch.constraints()) {
            addConstraint(system, id, constraint);
        }
        for (auto& [id, arc] : m_arcs) {
            system.addConstraintArcRules(arc, 0);
        }
    }

    GCS::VEC_pD& unknowns() { return m_unknowns; }

    bool isFinite() const
    {
        return std::ranges::all_of(m_params, [](double value) { return std::isfinite(value); });
    }

    void writeBack(Sketch& sketch) const
    {
        for (auto& [id, point] : sketch.points()) {
            const std::size_t index = m_pointIndex.at(id);
            sketch.point(id)->pos = {m_params[index], m_params[index + 1]};
        }
        for (const auto& [id, circle] : m_circles) {
            sketch.curve(id)->radius = *circle.rad;
        }
        for (const auto& [id, arc] : m_arcs) {
            sketch.curve(id)->radius = *arc.rad;
        }
    }

    std::set<Id> fullyConstrained(const GCS::VEC_pD& dependent) const
    {
        const std::unordered_set<const double*> free(dependent.begin(), dependent.end());
        const auto pointFixed = [&](Id id) {
            const std::size_t index = m_pointIndex.at(id);
            return !free.contains(&m_params[index]) && !free.contains(&m_params[index + 1]);
        };

        std::set<Id> result;
        for (const auto& [id, point] : m_sketch.points()) {
            if (pointFixed(id)) {
                result.insert(id);
            }
        }
        for (const auto& [id, curve] : m_sketch.curves()) {
            bool fixed = std::ranges::all_of(curve.points, pointFixed);
            if (auto it = m_circles.find(id); it != m_circles.end()) {
                fixed = fixed && !free.contains(it->second.rad);
            }
            if (auto it = m_arcs.find(id); it != m_arcs.end()) {
                fixed = fixed && !free.contains(it->second.rad) && !free.contains(it->second.startAngle) &&
                        !free.contains(it->second.endAngle);
            }
            if (fixed) {
                result.insert(id);
            }
        }
        return result;
    }

private:
    std::size_t push(double value)
    {
        m_params.push_back(value);
        return m_params.size() - 1;
    }

    void buildCurve(Id id, const Curve& curve)
    {
        switch (curve.kind) {
        case CurveKind::Line: {
            GCS::Line line;
            line.p1 = m_points.at(curve.points[0]);
            line.p2 = m_points.at(curve.points[1]);
            m_lines.emplace(id, line);
            break;
        }
        case CurveKind::Circle: {
            GCS::Circle circle;
            circle.center = m_points.at(curve.points[0]);
            circle.rad = &m_params[push(curve.radius)];
            m_circles.emplace(id, circle);
            break;
        }
        case CurveKind::Arc: {
            const Vec2 c = m_sketch.point(curve.points[0])->pos;
            const Vec2 s = m_sketch.point(curve.points[1])->pos;
            const Vec2 e = m_sketch.point(curve.points[2])->pos;
            const double start = std::atan2(s.y - c.y, s.x - c.x);
            double end = std::atan2(e.y - c.y, e.x - c.x);
            if (end <= start) {
                end += 2.0 * std::numbers::pi;
            }
            GCS::Arc arc;
            arc.center = m_points.at(curve.points[0]);
            arc.start = m_points.at(curve.points[1]);
            arc.end = m_points.at(curve.points[2]);
            arc.rad = &m_params[push(curve.radius)];
            arc.startAngle = &m_params[push(start)];
            arc.endAngle = &m_params[push(end)];
            m_arcs.emplace(id, arc);
            break;
        }
        }
    }

    void collectUnknowns()
    {
        std::set<Id> fixedPoints;
        for (const auto& [id, point] : m_sketch.points()) {
            if (point.fixed) {
                fixedPoints.insert(id);
            }
        }
        std::set<Id> fixedCurves;
        for (const auto& [id, curve] : m_sketch.curves()) {
            if (curve.fixed) {
                fixedCurves.insert(id);
                fixedPoints.insert(curve.points.begin(), curve.points.end());
            }
        }
        for (const auto& [id, index] : m_pointIndex) {
            if (!fixedPoints.contains(id)) {
                m_unknowns.push_back(&m_params[index]);
                m_unknowns.push_back(&m_params[index + 1]);
            }
        }
        for (auto& [id, circle] : m_circles) {
            if (!fixedCurves.contains(id)) {
                m_unknowns.push_back(circle.rad);
            }
        }
        for (auto& [id, arc] : m_arcs) {
            if (!fixedCurves.contains(id)) {
                m_unknowns.push_back(arc.rad);
                m_unknowns.push_back(arc.startAngle);
                m_unknowns.push_back(arc.endAngle);
            }
        }
    }

    GCS::Point* pointRef(Id id)
    {
        auto it = m_points.find(id);
        return it == m_points.end() ? nullptr : &it->second;
    }
    GCS::Line* lineRef(Id id)
    {
        auto it = m_lines.find(id);
        return it == m_lines.end() ? nullptr : &it->second;
    }
    GCS::Circle* circleRef(Id id)
    {
        auto it = m_circles.find(id);
        return it == m_circles.end() ? nullptr : &it->second;
    }
    GCS::Arc* arcRef(Id id)
    {
        auto it = m_arcs.find(id);
        return it == m_arcs.end() ? nullptr : &it->second;
    }
    GCS::Curve* curveRef(Id id)
    {
        if (GCS::Line* line = lineRef(id)) {
            return line;
        }
        if (GCS::Arc* arc = arcRef(id)) {
            return arc;
        }
        return circleRef(id);
    }

    std::vector<Id> endsOf(Id id) const
    {
        const Curve* curve = m_sketch.curve(id);
        if (!curve) {
            return {};
        }
        switch (curve->kind) {
        case CurveKind::Line:
            return {curve->points[0], curve->points[1]};
        case CurveKind::Arc:
            return {curve->points[1], curve->points[2]};
        case CurveKind::Circle:
            return {};
        }
        return {};
    }

    std::optional<Id> sharedEnd(Id a, Id b) const
    {
        const std::vector<Id> other = endsOf(b);
        for (Id end : endsOf(a)) {
            if (std::ranges::find(other, end) != other.end()) {
                return end;
            }
        }
        return std::nullopt;
    }

    double* value(double v) { return &m_params[push(v)]; }

    void addConstraint(GCS::System& sys, Id tag, const Constraint& c)
    {
        const auto ref = [&](std::size_t i) { return i < c.refs.size() ? c.refs[i] : 0; };
        GCS::Point* p0 = pointRef(ref(0));
        GCS::Point* p1 = pointRef(ref(1));
        GCS::Line* l0 = lineRef(ref(0));
        GCS::Line* l1 = lineRef(ref(1));
        GCS::Circle* c0 = circleRef(ref(0));
        GCS::Circle* c1 = circleRef(ref(1));
        GCS::Arc* a0 = arcRef(ref(0));
        GCS::Arc* a1 = arcRef(ref(1));

        switch (c.kind) {
        case ConstraintKind::Coincident:
            if (p0 && p1) {
                sys.addConstraintP2PCoincident(*p0, *p1, tag);
            }
            break;
        case ConstraintKind::Horizontal:
            if (l0) {
                sys.addConstraintHorizontal(*l0, tag);
            } else if (p0 && p1) {
                sys.addConstraintHorizontal(*p0, *p1, tag);
            }
            break;
        case ConstraintKind::Vertical:
            if (l0) {
                sys.addConstraintVertical(*l0, tag);
            } else if (p0 && p1) {
                sys.addConstraintVertical(*p0, *p1, tag);
            }
            break;
        case ConstraintKind::Parallel:
            if (l0 && l1) {
                sys.addConstraintParallel(*l0, *l1, tag);
            }
            break;
        case ConstraintKind::Perpendicular:
            if (l0 && l1) {
                sys.addConstraintPerpendicular(*l0, *l1, tag);
            }
            break;
        case ConstraintKind::Tangent:
            addTangent(sys, tag, c);
            break;
        case ConstraintKind::Equal:
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
            }
            break;
        case ConstraintKind::PointOnCurve:
            if (p0 && l1) {
                sys.addConstraintPointOnLine(*p0, *l1, tag);
            } else if (p0 && c1) {
                sys.addConstraintPointOnCircle(*p0, *c1, tag);
            } else if (p0 && a1) {
                sys.addConstraintPointOnArc(*p0, *a1, tag);
            }
            break;
        case ConstraintKind::Midpoint:
            if (p0 && l1) {
                sys.addConstraintPointOnLine(*p0, *l1, tag);
                sys.addConstraintPointOnPerpBisector(*p0, *l1, tag);
            }
            break;
        case ConstraintKind::Distance:
            if (p0 && p1) {
                sys.addConstraintP2PDistance(*p0, *p1, value(c.value), tag);
            } else if (l0 && c.refs.size() == 1) {
                sys.addConstraintP2PDistance(l0->p1, l0->p2, value(c.value), tag);
            } else if (p0 && l1) {
                sys.addConstraintP2LDistance(*p0, *l1, value(c.value), tag);
            }
            break;
        case ConstraintKind::HorizontalDistance:
            if (p0 && p1) {
                const double sign = *p1->x >= *p0->x ? 1.0 : -1.0;
                sys.addConstraintDifference(p0->x, p1->x, value(sign * c.value), tag);
            }
            break;
        case ConstraintKind::VerticalDistance:
            if (p0 && p1) {
                const double sign = *p1->y >= *p0->y ? 1.0 : -1.0;
                sys.addConstraintDifference(p0->y, p1->y, value(sign * c.value), tag);
            }
            break;
        case ConstraintKind::Radius:
            if (c0) {
                sys.addConstraintCircleRadius(*c0, value(c.value), tag);
            } else if (a0) {
                sys.addConstraintArcRadius(*a0, value(c.value), tag);
            }
            break;
        case ConstraintKind::Diameter:
            if (c0) {
                sys.addConstraintCircleDiameter(*c0, value(c.value), tag);
            } else if (a0) {
                sys.addConstraintArcDiameter(*a0, value(c.value), tag);
            }
            break;
        case ConstraintKind::Angle:
            if (l0 && l1) {
                const double d0x = *l0->p2.x - *l0->p1.x;
                const double d0y = *l0->p2.y - *l0->p1.y;
                const double d1x = *l1->p2.x - *l1->p1.x;
                const double d1y = *l1->p2.y - *l1->p1.y;
                const double sign = d0x * d1y - d0y * d1x >= 0.0 ? 1.0 : -1.0;
                sys.addConstraintL2LAngle(*l0, *l1, value(sign * c.value * kDegToRad), tag);
            }
            break;
        case ConstraintKind::Symmetric:
            if (GCS::Line* axis = lineRef(ref(2)); p0 && p1 && axis) {
                sys.addConstraintP2PSymmetric(*p0, *p1, *axis, tag);
            }
            break;
        }
    }

    void addTangent(GCS::System& sys, Id tag, const Constraint& c)
    {
        if (c.refs.size() != 2) {
            return;
        }
        Id first = c.refs[0];
        Id second = c.refs[1];
        if (const std::optional<Id> joint = sharedEnd(first, second)) {
            GCS::Curve* a = curveRef(first);
            GCS::Curve* b = curveRef(second);
            GCS::Point& p = m_points.at(*joint);
            const double angle = sys.calculateAngleViaPoint(*a, *b, p);
            const bool reversed = std::abs(std::remainder(angle, 2.0 * std::numbers::pi)) > std::numbers::pi / 2;
            sys.addConstraintAngleViaPoint(*a, *b, p, value(reversed ? std::numbers::pi : 0.0), tag);
            return;
        }
        if (lineRef(second)) {
            std::swap(first, second);
        }
        if (GCS::Line* line = lineRef(first)) {
            if (GCS::Circle* circle = circleRef(second)) {
                sys.addConstraintTangent(*line, *circle, tag);
            } else if (GCS::Arc* arc = arcRef(second)) {
                sys.addConstraintTangent(*line, *arc, tag);
            }
            return;
        }
        if (arcRef(first) && circleRef(second)) {
            std::swap(first, second);
        }
        if (GCS::Circle* circle = circleRef(first)) {
            if (GCS::Circle* other = circleRef(second)) {
                sys.addConstraintTangent(*circle, *other, tag);
            } else if (GCS::Arc* arc = arcRef(second)) {
                sys.addConstraintTangent(*circle, *arc, tag);
            }
        } else if (GCS::Arc* arc = arcRef(first)) {
            if (GCS::Arc* other = arcRef(second)) {
                sys.addConstraintTangent(*arc, *other, tag);
            }
        }
    }

    const Sketch& m_sketch;
    std::vector<double> m_params;
    std::map<Id, std::size_t> m_pointIndex;
    std::map<Id, GCS::Point> m_points;
    std::map<Id, GCS::Line> m_lines;
    std::map<Id, GCS::Circle> m_circles;
    std::map<Id, GCS::Arc> m_arcs;
    GCS::VEC_pD m_unknowns;
};

std::vector<Id> toIds(const GCS::VEC_I& tags)
{
    std::vector<Id> ids;
    for (int tag : tags) {
        if (tag > 0) {
            ids.push_back(tag);
        }
    }
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

SolveResult solve(Sketch& sketch)
{
    SolverModel model(sketch);
    GCS::System system;
    model.addConstraints(system);
    system.declareUnknowns(model.unknowns());
    system.initSolution();

    SolveResult result;
    result.dof = system.dofsNumber();
    GCS::VEC_I tags;
    system.getConflicting(tags);
    result.conflicting = toIds(tags);
    system.getRedundant(tags);
    result.redundant = toIds(tags);

    const int status = system.solve(true);
    result.solved = status == GCS::Success && result.conflicting.empty();
    if (result.solved) {
        system.applySolution();
        if (!model.isFinite()) {
            K137_WARN(Sketch, "Solver returned non-finite values, keeping the previous positions");
            result.solved = false;
        }
    }
    if (result.solved) {
        model.writeBack(sketch);
    } else {
        system.undoSolution();
    }

    GCS::VEC_pD dependent;
    system.getDependentParams(dependent);
    if (dependent.empty() && result.dof > 0) {
        dependent = model.unknowns();
    }
    result.fullyConstrained = model.fullyConstrained(dependent);
    K137_TRACE(Sketch, "Solve status {} solved {} dof {} conflicting {} redundant {}", status, result.solved,
               result.dof, result.conflicting.size(), result.redundant.size());
    return result;
}

} // namespace kalve137::sketch
