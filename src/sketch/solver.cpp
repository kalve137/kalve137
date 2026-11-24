// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/solver.h"

#include "log/log.h"
#include <planegcs/GCS.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <unordered_set>
#include <vector>

namespace kalve137::sketch {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kQuarterTurn = std::numbers::pi / 2.0;
constexpr double kShortest = 1e-6;

Vec2 directionOf(const GCS::Line& line)
{
    return Vec2{*line.p2.x, *line.p2.y} - Vec2{*line.p1.x, *line.p1.y};
}

struct OffsetEnd {
    std::size_t pair = 0;
    std::size_t end = 0;
};

class SolverModel {
public:
    SolverModel(const Sketch& sketch, const Sketch* signs, bool holdOffsetSources)
        : m_sketch(sketch), m_signs(signs != nullptr ? *signs : sketch)
    {
        if (holdOffsetSources) {
            for (const auto& [id, constraint] : sketch.constraints()) {
                for (const OffsetPair& pair : offsetPairs(constraint)) {
                    m_held.insert(pair.source);
                }
            }
        }
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

    void addTargets(GCS::System& system, const std::vector<DragTarget>& targets, std::vector<double>& goals)
    {
        goals.reserve(2 * targets.size());
        for (const DragTarget& target : targets) {
            const auto it = m_points.find(target.point);
            if (it == m_points.end() || std::ranges::find(m_unknowns, it->second.x) == m_unknowns.end()) {
                continue;
            }
            goals.push_back(target.pos.x);
            system.addConstraintEqual(it->second.x, &goals.back(), GCS::DefaultTemporaryConstraint);
            goals.push_back(target.pos.y);
            system.addConstraintEqual(it->second.y, &goals.back(), GCS::DefaultTemporaryConstraint);
        }
    }

    bool isFinite() const
    {
        return std::ranges::all_of(m_params, [](double value) { return std::isfinite(value); });
    }

    bool keepsShape() const
    {
        const auto positive = [](const auto& entry) { return *entry.second.rad > kShortest; };
        if (!std::ranges::all_of(m_circles, positive) || !std::ranges::all_of(m_arcs, positive)) {
            return false;
        }
        for (const auto& [id, constraint] : m_sketch.constraints()) {
            for (const OffsetPair& pair : offsetPairs(constraint)) {
                const auto source = m_lines.find(pair.source);
                const auto offset = m_lines.find(pair.offset);
                if (source == m_lines.end() || offset == m_lines.end()) {
                    continue;
                }
                const Vec2 along = directionOf(source->second);
                if (dot(along, directionOf(offset->second)) <= kShortest * length(along)) {
                    return false;
                }
            }
        }
        return true;
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
            if (curve.fixed || m_held.contains(id)) {
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

    Vec2 signPos(Id id) const
    {
        if (const Point* point = m_signs.point(id)) {
            return point->pos;
        }
        const std::size_t index = m_pointIndex.at(id);
        return {m_params[index], m_params[index + 1]};
    }

    Vec2 signDirection(Id line) const
    {
        const Curve* curve = m_sketch.curve(line);
        return signPos(curve->points[1]) - signPos(curve->points[0]);
    }

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
                const double sign = signPos(ref(1)).x >= signPos(ref(0)).x ? 1.0 : -1.0;
                sys.addConstraintDifference(p0->x, p1->x, value(sign * c.value), tag);
            }
            break;
        case ConstraintKind::VerticalDistance:
            if (p0 && p1) {
                const double sign = signPos(ref(1)).y >= signPos(ref(0)).y ? 1.0 : -1.0;
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
                const Vec2 d0 = signDirection(ref(0));
                const Vec2 d1 = signDirection(ref(1));
                const double sign = cross(d0, d1) >= 0.0 ? 1.0 : -1.0;
                sys.addConstraintL2LAngle(*l0, *l1, value(sign * c.value * kDegToRad), tag);
            }
            break;
        case ConstraintKind::Symmetric:
            if (GCS::Line* axis = lineRef(ref(2)); p0 && p1 && axis) {
                sys.addConstraintP2PSymmetric(*p0, *p1, *axis, tag);
            }
            break;
        case ConstraintKind::Offset:
            addOffset(sys, tag, c);
            break;
        }
    }

    GCS::Point helperPoint(Vec2 at)
    {
        const std::size_t index = push(at.x);
        push(at.y);
        m_unknowns.push_back(&m_params[index]);
        m_unknowns.push_back(&m_params[index + 1]);
        return {&m_params[index], &m_params[index + 1]};
    }

    GCS::Line& helperLine(const GCS::Point& from, const GCS::Point& to)
    {
        GCS::Line& line = m_helperLines.emplace_back();
        line.p1 = from;
        line.p2 = to;
        return line;
    }

    void addOffset(GCS::System& sys, Id tag, const Constraint& c)
    {
        const std::vector<OffsetPair> pairs = offsetPairs(c);
        for (const OffsetPair& pair : pairs) {
            const Curve* source = m_sketch.curve(pair.source);
            const Curve* offset = m_sketch.curve(pair.offset);
            if (source == nullptr || offset == nullptr || source->kind != offset->kind) {
                return;
            }
        }
        std::map<Id, std::vector<OffsetEnd>> ends;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            const std::vector<Id> offsetEnds = endsOf(pairs[i].offset);
            for (std::size_t k = 0; k < offsetEnds.size(); ++k) {
                ends[offsetEnds[k]].push_back({.pair = i, .end = k});
            }
        }
        std::vector<std::size_t> parent(pairs.size());
        std::iota(parent.begin(), parent.end(), std::size_t{0});
        const auto rootOf = [&](std::size_t i) {
            while (parent[i] != i) {
                i = parent[i];
            }
            return i;
        };
        std::vector<bool> parallel(pairs.size(), true);
        for (const auto& [point, touching] : ends) {
            if (touching.size() == 1) {
                anchorEnd(sys, tag, pairs[touching[0].pair], touching[0].end);
                continue;
            }
            const auto joint =
                std::ranges::find(c.joints, point, [](const OffsetJoint& stored) { return stored.point; });
            if (touching.size() != 2 || joint == c.joints.end() || !joint->tangent) {
                continue;
            }
            const OffsetEnd& anchor = lineRef(pairs[touching[0].pair].offset) != nullptr ? touching[1] : touching[0];
            anchorEnd(sys, tag, pairs[anchor.pair], anchor.end);
            const std::size_t first = rootOf(touching[0].pair);
            const std::size_t second = rootOf(touching[1].pair);
            if (first != second) {
                parent[std::max(first, second)] = std::min(first, second);
                continue;
            }
            for (const OffsetEnd& end : touching) {
                if (lineRef(pairs[end.pair].offset) != nullptr) {
                    parallel[end.pair] = false;
                    break;
                }
            }
        }
        double* distance = value(c.value);
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            const bool root = rootOf(i) == i;
            GCS::Line* sourceLine = lineRef(pairs[i].source);
            GCS::Line* offsetLine = lineRef(pairs[i].offset);
            if (sourceLine != nullptr && offsetLine != nullptr) {
                addLineOffset(sys, tag, pairs[i], *sourceLine, *offsetLine, parallel[i], root ? distance : nullptr);
            } else {
                addRoundOffset(sys, tag, pairs[i], root ? std::optional(c.value) : std::nullopt);
            }
        }
    }

    void anchorEnd(GCS::System& sys, Id tag, const OffsetPair& pair, std::size_t end)
    {
        GCS::Point& from = m_points.at(endsOf(pair.source).at(end));
        GCS::Point& to = m_points.at(endsOf(pair.offset).at(end));
        if (GCS::Line* line = lineRef(pair.source)) {
            sys.addConstraintL2LAngle(*line, helperLine(from, to), value(pair.side * kQuarterTurn), tag);
            return;
        }
        GCS::Point& sourceCenter = m_points.at(m_sketch.curve(pair.source)->points[0]);
        GCS::Point& offsetCenter = m_points.at(m_sketch.curve(pair.offset)->points[0]);
        sys.addConstraintL2LAngle(helperLine(sourceCenter, from), helperLine(offsetCenter, to), value(0.0), tag);
    }

    void addLineOffset(GCS::System& sys, Id tag, const OffsetPair& pair, GCS::Line& source, GCS::Line& offset,
                       bool parallel, double* distance)
    {
        if (parallel) {
            sys.addConstraintParallel(offset, source, tag);
        }
        if (distance == nullptr) {
            return;
        }
        const double side = pair.side;
        const Vec2 from{*source.p1.x, *source.p1.y};
        const Vec2 along = Vec2{*source.p2.x, *source.p2.y} - from;
        const double length = std::hypot(along.x, along.y);
        const Vec2 normal = length > 0.0 ? Vec2{-along.y / length, along.x / length} : Vec2{0.0, 1.0};
        GCS::Point foot = helperPoint(from + normal * (side * *distance));
        sys.addConstraintP2PDistance(source.p1, foot, distance, tag);
        sys.addConstraintL2LAngle(source, helperLine(source.p1, foot), value(side * kQuarterTurn), tag);
        sys.addConstraintPointOnLine(foot, offset, tag);
    }

    void addRoundOffset(GCS::System& sys, Id tag, const OffsetPair& pair, std::optional<double> distance)
    {
        const Curve* source = m_sketch.curve(pair.source);
        const Curve* offset = m_sketch.curve(pair.offset);
        double* sourceRadius = radiusOf(pair.source);
        double* offsetRadius = radiusOf(pair.offset);
        if (sourceRadius == nullptr || offsetRadius == nullptr) {
            return;
        }
        if (source->points[0] != offset->points[0]) {
            sys.addConstraintP2PCoincident(m_points.at(offset->points[0]), m_points.at(source->points[0]), tag);
        }
        if (distance) {
            sys.addConstraintDifference(sourceRadius, offsetRadius, value(pair.side * *distance), tag);
        }
    }

    double* radiusOf(Id id)
    {
        if (GCS::Circle* circle = circleRef(id)) {
            return circle->rad;
        }
        if (GCS::Arc* arc = arcRef(id)) {
            return arc->rad;
        }
        return nullptr;
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
    const Sketch& m_signs;
    std::set<Id> m_held;
    std::deque<double> m_params;
    std::map<Id, std::size_t> m_pointIndex;
    std::map<Id, GCS::Point> m_points;
    std::map<Id, GCS::Line> m_lines;
    std::map<Id, GCS::Circle> m_circles;
    std::map<Id, GCS::Arc> m_arcs;
    std::deque<GCS::Line> m_helperLines;
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

SolveResult solveOnce(Sketch& sketch, const Sketch* signs, const std::vector<DragTarget>& targets,
                      bool holdOffsetSources)
{
    SolverModel model(sketch, signs, holdOffsetSources);
    GCS::System system;
    model.addConstraints(system);
    std::vector<double> goals;
    model.addTargets(system, targets, goals);
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
        } else if (!model.keepsShape()) {
            K137_DEBUG(Sketch, "Solve turned a curve inside out, keeping the previous positions");
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

SolveResult solveWithSigns(Sketch& sketch, const Sketch* before, const std::vector<DragTarget>& targets)
{
    const bool offsets = std::ranges::any_of(
        sketch.constraints(), [](const auto& entry) { return entry.second.kind == ConstraintKind::Offset; });
    if (!offsets) {
        return solveOnce(sketch, before, targets, false);
    }
    Sketch settled = sketch;
    if (solveOnce(settled, before, targets, true).solved) {
        SolveResult result = solveOnce(settled, before, targets, false);
        if (result.solved) {
            sketch = std::move(settled);
            return result;
        }
    }
    return solveOnce(sketch, before, targets, false);
}

} // namespace

SolveResult solve(Sketch& sketch, const std::vector<DragTarget>& targets)
{
    return solveWithSigns(sketch, nullptr, targets);
}

SolveResult solveMoved(Sketch& sketch, const Sketch& before)
{
    return solveWithSigns(sketch, &before, {});
}

} // namespace kalve137::sketch
