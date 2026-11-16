// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rebuild/sketch.h"

#include "lang/format.h"
#include "solve/solve.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <string>

namespace kalve137::rebuild {

namespace {

using lang::Diagnostic;
using lang::EvalError;
using lang::SketchItem;
using lang::Statement;

std::string quoted(const SketchItem& item)
{
    return "'" + lang::format(item) + "'";
}

std::string listed(const std::vector<std::string>& words)
{
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        text += (i == 0 ? "" : (i + 1 == words.size() ? " and " : ", ")) + words[i];
    }
    return text;
}

std::string position(const solve::Point& p)
{
    return "(" + lang::formatLength(p.u) + ", " + lang::formatLength(p.v) + ")";
}

Outline writtenOutline(const SketchItem& item, const lang::Params& params)
{
    const double cx = lengthOf(*item.center.x, params, "center x", false);
    const double cy = lengthOf(*item.center.y, params, "center y", false);
    Outline outline{item.name, {}, {}};
    if (item.kind == SketchItem::Kind::Rect) {
        const double w = lengthOf(*item.size.x, params, "width of " + item.name, true) / 2.0;
        const double h = lengthOf(*item.size.y, params, "height of " + item.name, true) / 2.0;
        const kernel::Point2 corners[] = {{cx - w, cy - h}, {cx + w, cy - h}, {cx + w, cy + h}, {cx - w, cy + h}};
        for (int i = 0; i < 4; ++i) {
            kernel::Segment side;
            side.a = corners[i];
            side.b = corners[(i + 1) % 4];
            outline.loop.push_back(side);
            outline.sides.emplace_back();
        }
        return outline;
    }
    kernel::Segment wall;
    wall.kind = kernel::Segment::Kind::Circle;
    wall.c = {cx, cy};
    wall.radius = lengthOf(*item.diameter, params, "diameter of " + item.name, true) / 2.0;
    outline.loop.push_back(wall);
    outline.sides.emplace_back("wall");
    return outline;
}

class Solved {
public:
    Solved(const Statement& sketch, const lang::Params& params) : m_sketch(sketch), m_params(params)
    {
        std::map<std::string, const SketchItem*> pins;
        for (const SketchItem& item : sketch.items) {
            if (item.kind == SketchItem::Kind::Constraint && item.call->text == "at" && item.center.x) {
                pins[item.call->args.at(0)->text] = &item;
            }
        }
        for (const SketchItem& item : sketch.items) {
            if (item.kind != SketchItem::Kind::Point) {
                continue;
            }
            m_refs[item.name] = {solve::Ref::Kind::Point, static_cast<int>(m_data.points.size())};
            const auto pin = pins.find(item.name);
            const lang::Point* start = item.center.x ? &item.center : nullptr;
            if (start == nullptr && pin != pins.end()) {
                start = &pin->second->center;
            }
            if (start == nullptr) {
                throw EvalError(item.span, "Point " + item.name + " has no guess for the solver to start from",
                                "Write point " + item.name + " near (x, y), or pin it with at(" + item.name +
                                    ") = (x, y)",
                                "guess");
            }
            m_data.points.push_back({lengthOf(*start->x, params, "x of point " + item.name, false),
                                     lengthOf(*start->y, params, "y of point " + item.name, false)});
            m_pointItems.push_back(&item);
        }
        for (const SketchItem& item : sketch.items) {
            if (item.kind == SketchItem::Kind::Line) {
                m_refs[item.name] = {solve::Ref::Kind::Line, static_cast<int>(m_data.lines.size())};
                m_data.lines.push_back({point(item.refs[0]), point(item.refs[1])});
            } else if (item.kind == SketchItem::Kind::Arc) {
                m_refs[item.name] = {solve::Ref::Kind::Arc, static_cast<int>(m_data.arcs.size())};
                m_data.arcs.push_back({point(item.refs[2]), point(item.refs[0]), point(item.refs[1])});
                m_arcItems.push_back(&item);
            } else if (item.kind == SketchItem::Kind::Circle && !item.refs.empty()) {
                const double radius = lengthOf(*item.diameter, params, "diameter of " + item.name, true) / 2.0;
                const solve::Ref ref{solve::Ref::Kind::Circle, static_cast<int>(m_data.circles.size())};
                m_refs[item.name] = ref;
                m_data.circles.push_back({point(item.refs[0]), radius});
                m_circleItems.push_back(&item);
                m_data.constraints.push_back({solve::Constraint::Kind::Radius, {ref}, radius});
                m_sources.push_back(&item);
            }
        }
        for (const SketchItem& item : sketch.items) {
            if (item.kind == SketchItem::Kind::Constraint) {
                addConstraint(item);
            }
        }
    }

    bool empty() const { return m_data.points.empty(); }

    void run(std::vector<Diagnostic>& warnings)
    {
        m_solution = solve::solve(m_data);
        switch (m_solution.status) {
        case solve::Solution::Status::Solved:
            break;
        case solve::Solution::Status::Conflicting:
            throw EvalError(source(m_solution.conflicting).span,
                            "Sketch '" + m_sketch.name +
                                "' asks for things that cannot all hold. Without any one of these the rest would: " +
                                quotes(m_solution.conflicting),
                            "Remove one of them or change a dimension so they agree", "conflict");
        case solve::Solution::Status::Redundant:
            throw EvalError(source(m_solution.redundant).span,
                            "Sketch '" + m_sketch.name +
                                "' says the same thing more than once. Each of these follows from the others: " +
                                quotes(m_solution.redundant),
                            "Remove one of them, like " +
                                quoted(*m_sources.at(static_cast<std::size_t>(m_solution.redundant.back()))) +
                                ", the last one written. A repeat holds only while the numbers agree, so a param "
                                "change could break it",
                            "over");
        case solve::Solution::Status::Failed:
            throw EvalError(m_sketch.span, "Sketch '" + m_sketch.name + "' did not solve from its guesses",
                            "Move the guesses nearer to the shape the constraints describe, or check that the "
                            "dimensions can be met",
                            "unsolved");
        }
        if (m_solution.freedom > 0) {
            std::vector<std::string> loose;
            loose.reserve(m_solution.freePoints.size() + m_solution.freeCircles.size());
            for (const int i : m_solution.freePoints) {
                loose.push_back(m_pointItems[static_cast<std::size_t>(i)]->name);
            }
            for (const int i : m_solution.freeCircles) {
                loose.push_back("the size of " + m_circleItems[static_cast<std::size_t>(i)]->name);
            }
            const int n = m_solution.freedom;
            warnings.push_back({Diagnostic::Severity::Warning, "free", m_sketch.span, m_sketch.name,
                                "Sketch '" + m_sketch.name + "' has " + std::to_string(n) + " degree" +
                                    (n == 1 ? "" : "s") + " of freedom left. Free to move: " + listed(loose),
                                "What is free stays at its guess, so the shape depends on a guess and a param "
                                "change will not move it. Add constraints or dimensions until nothing is free"});
        }
    }

    model::SolvedSketch report() const
    {
        model::SolvedSketch out{m_sketch.name, m_solution.freedom, {}, {}};
        for (std::size_t i = 0; i < m_pointItems.size(); ++i) {
            const kernel::Point2 p = solvedAt(static_cast<int>(i));
            const bool loose = std::find(m_solution.freePoints.begin(), m_solution.freePoints.end(),
                                         static_cast<int>(i)) != m_solution.freePoints.end();
            out.points.push_back({m_pointItems[i]->name, p.u, p.v, loose});
        }
        for (const SketchItem& item : m_sketch.items) {
            if (item.kind == SketchItem::Kind::Line) {
                const kernel::Point2 a = solvedAt(point(item.refs[0]));
                const kernel::Point2 b = solvedAt(point(item.refs[1]));
                out.lines.push_back({item.name, item.refs[0], item.refs[1], std::hypot(b.u - a.u, b.v - a.v)});
            }
        }
        return out;
    }

    std::vector<Outline> loops() const
    {
        std::vector<Outline> out;
        for (std::size_t i = 0; i < m_circleItems.size(); ++i) {
            const solve::Circle& circle = m_data.circles[i];
            kernel::Segment wall;
            wall.kind = kernel::Segment::Kind::Circle;
            wall.c = solvedAt(circle.center);
            wall.radius = m_solution.circleRadii[i];
            out.push_back({m_circleItems[i]->name, {wall}, {"wall"}});
        }
        for (const std::vector<Step>& walk : walks(m_data)) {
            if (const std::optional<Trouble> wrong = trouble(m_data, m_solution.points, walk)) {
                throw EvalError(m_sketch.span, wrong->message, fixFor(*wrong), wrong->code);
            }
            Outline outline{m_sketch.name, {}, {}};
            for (const Step& step : walk) {
                outline.loop.push_back(segment(m_data, m_solution.points, step));
                outline.sides.push_back(step.item->name);
            }
            out.push_back(std::move(outline));
        }
        probe();
        return out;
    }

private:
    struct Step {
        const SketchItem* item;
        bool arc;
        int index;
        bool forward;
    };

    struct Trouble {
        std::string code;
        std::string message;
    };

    int point(const std::string& name) const { return m_refs.at(name).index; }

    void addConstraint(const SketchItem& item)
    {
        using Kind = solve::Constraint::Kind;
        const std::string& f = item.call->text;
        if (f == "at") {
            const solve::Ref ref = m_refs.at(item.call->args[0]->text);
            m_data.constraints.push_back(
                {Kind::X, {ref}, lengthOf(*item.center.x, m_params, "x of " + quoted(item), false)});
            m_data.constraints.push_back(
                {Kind::Y, {ref}, lengthOf(*item.center.y, m_params, "y of " + quoted(item), false)});
            m_sources.push_back(&item);
            m_sources.push_back(&item);
            return;
        }
        static const std::map<std::string, Kind> kKinds{{"x", Kind::X},
                                                        {"y", Kind::Y},
                                                        {"coincident", Kind::Coincident},
                                                        {"horizontal", Kind::Horizontal},
                                                        {"vertical", Kind::Vertical},
                                                        {"parallel", Kind::Parallel},
                                                        {"perpendicular", Kind::Perpendicular},
                                                        {"equal", Kind::Equal},
                                                        {"tangent", Kind::Tangent},
                                                        {"distance", Kind::Distance},
                                                        {"length", Kind::Distance},
                                                        {"radius", Kind::Radius},
                                                        {"angle", Kind::Angle}};
        solve::Constraint constraint{kKinds.at(f), {}, 0.0};
        for (const lang::ExprPtr& arg : item.call->args) {
            constraint.refs.push_back(m_refs.at(arg->text));
        }
        if (item.value) {
            const lang::Quantity q = lang::evaluate(*item.value, m_params);
            if (q.value <= 0.0 && f != "x" && f != "y") {
                throw EvalError(item.value->span,
                                "The value of " + quoted(item) + " must be above zero, and it is " + lang::toText(q),
                                f == "distance" ? "To make two points meet, write coincident(a, b)" : "", "constraint");
            }
            constraint.value = q.value;
        }
        m_data.constraints.push_back(std::move(constraint));
        m_sources.push_back(&item);
    }

    const SketchItem& source(const std::vector<int>& constraints) const
    {
        return *m_sources.at(static_cast<std::size_t>(constraints.front()));
    }

    std::string quotes(const std::vector<int>& constraints) const
    {
        std::vector<std::string> words;
        const SketchItem* previous = nullptr;
        for (const int i : constraints) {
            const SketchItem* item = m_sources.at(static_cast<std::size_t>(i));
            if (item != previous) {
                words.push_back(quoted(*item));
            }
            previous = item;
        }
        return listed(words);
    }

    kernel::Point2 solvedAt(int point) const
    {
        const solve::Point& p = m_solution.points.at(static_cast<std::size_t>(point));
        return {p.u, p.v};
    }

    void probe() const
    {
        if (m_solution.freedom > 0) {
            return;
        }
        const std::vector<std::vector<Step>> loops = walks(m_data);
        constexpr int kProbes = 8;
        constexpr double kGoldenAngle = 2.399963229728653;
        double minU = m_data.points.front().u;
        double maxU = minU;
        double minV = m_data.points.front().v;
        double maxV = minV;
        for (const solve::Point& p : m_data.points) {
            minU = std::min(minU, p.u);
            maxU = std::max(maxU, p.u);
            minV = std::min(minV, p.v);
            maxV = std::max(maxV, p.v);
        }
        const double size = std::max(std::hypot(maxU - minU, maxV - minV), 1.0);
        const double reach = 0.1 * size;
        const double tolerance = 1e-6 * size;
        for (int k = 0; k < kProbes; ++k) {
            solve::Sketch moved = m_data;
            for (std::size_t i = 0; i < moved.points.size(); ++i) {
                const double angle = k * 2.0 * std::numbers::pi / kProbes + static_cast<double>(i) * kGoldenAngle;
                moved.points[i].u += reach * std::cos(angle);
                moved.points[i].v += reach * std::sin(angle);
            }
            const std::optional<std::vector<solve::Point>> other = solve::positions(moved);
            if (!other || std::ranges::any_of(
                              loops, [&](const auto& walk) { return trouble(m_data, *other, walk).has_value(); })) {
                continue;
            }
            std::vector<std::string> shifts;
            for (std::size_t i = 0; i < other->size(); ++i) {
                const solve::Point& here = m_solution.points[i];
                const solve::Point& there = (*other)[i];
                if (std::hypot(there.u - here.u, there.v - here.v) > tolerance) {
                    shifts.push_back(m_pointItems[i]->name + " at " + position(there) + " instead of " +
                                     position(here));
                }
            }
            if (!shifts.empty()) {
                throw EvalError(m_sketch.span,
                                "Sketch '" + m_sketch.name +
                                    "' meets its constraints in more than one shape near its guesses. Guesses a "
                                    "little off give " +
                                    listed(shifts),
                                "The guesses decide between these shapes. Add a constraint or dimension that tells "
                                "them apart, like a distance or angle on the points that move",
                                "branch");
            }
        }
    }

    static kernel::Point2 at(const std::vector<solve::Point>& points, int index)
    {
        const solve::Point& p = points.at(static_cast<std::size_t>(index));
        return {p.u, p.v};
    }

    kernel::Segment segment(const solve::Sketch& data, const std::vector<solve::Point>& points, const Step& step) const
    {
        kernel::Segment out;
        if (!step.arc) {
            const solve::Line& line = data.lines[static_cast<std::size_t>(step.index)];
            out.a = at(points, step.forward ? line.a : line.b);
            out.b = at(points, step.forward ? line.b : line.a);
            if (std::hypot(out.b.u - out.a.u, out.b.v - out.a.v) < 1e-7) {
                throw EvalError(step.item->span,
                                "Line '" + step.item->name + "' has no length in sketch '" + m_sketch.name + "'",
                                "Give it a length, or check the constraints that pull its ends together", "unsolved");
            }
            return out;
        }
        const solve::Arc& arc = data.arcs[static_cast<std::size_t>(step.index)];
        const kernel::Point2 c = at(points, arc.center);
        const kernel::Point2 s = at(points, arc.start);
        const kernel::Point2 e = at(points, arc.end);
        const double start = std::atan2(s.v - c.v, s.u - c.u);
        double sweep = std::atan2(e.v - c.v, e.u - c.u) - start;
        while (sweep <= 0.0) {
            sweep += 2.0 * std::numbers::pi;
        }
        const double radius = std::hypot(s.u - c.u, s.v - c.v);
        out.kind = kernel::Segment::Kind::Arc;
        out.c = c;
        out.m = {c.u + radius * std::cos(start + sweep / 2.0), c.v + radius * std::sin(start + sweep / 2.0)};
        out.a = step.forward ? s : e;
        out.b = step.forward ? e : s;
        return out;
    }

    static kernel::Point2 heading(const solve::Sketch& data, const std::vector<solve::Point>& points, const Step& step,
                                  bool end)
    {
        if (!step.arc) {
            const solve::Line& line = data.lines[static_cast<std::size_t>(step.index)];
            const kernel::Point2 a = at(points, line.a);
            const kernel::Point2 b = at(points, line.b);
            const double sign = step.forward ? 1.0 : -1.0;
            return {sign * (b.u - a.u), sign * (b.v - a.v)};
        }
        const solve::Arc& arc = data.arcs[static_cast<std::size_t>(step.index)];
        const kernel::Point2 c = at(points, arc.center);
        const kernel::Point2 p = at(points, (end == step.forward) ? arc.end : arc.start);
        const double sign = step.forward ? 1.0 : -1.0;
        return {-sign * (p.v - c.v), sign * (p.u - c.u)};
    }

    int startPoint(const solve::Sketch& data, const Step& step) const
    {
        if (step.arc) {
            const solve::Arc& arc = data.arcs[static_cast<std::size_t>(step.index)];
            return step.forward ? arc.start : arc.end;
        }
        const solve::Line& line = data.lines[static_cast<std::size_t>(step.index)];
        return step.forward ? line.a : line.b;
    }

    std::optional<Trouble> trouble(const solve::Sketch& data, const std::vector<solve::Point>& points,
                                   const std::vector<Step>& walk) const
    {
        const std::size_t steps = walk.size();
        for (std::size_t i = 0; i < steps; ++i) {
            const Step& next = walk[(i + 1) % steps];
            const kernel::Point2 out = heading(data, points, walk[i], true);
            const kernel::Point2 in = heading(data, points, next, false);
            const double scale = std::hypot(out.u, out.v) * std::hypot(in.u, in.v);
            if (scale > 0.0 && out.u * in.u + out.v * in.v < (-1.0 + 1e-9) * scale) {
                return Trouble{"cusp", "Sketch '" + m_sketch.name + "' doubles back on itself at point " +
                                           m_pointItems[static_cast<std::size_t>(startPoint(data, next))]->name +
                                           ", where " + walk[i].item->name + " meets " + next.item->name};
            }
        }
        struct Corner {
            kernel::Point2 written;
            kernel::Point2 solved;
            std::string name;
        };
        std::vector<Corner> corners;
        for (const Step& step : walk) {
            const kernel::Segment written = segment(data, data.points, step);
            const kernel::Segment solved = segment(data, points, step);
            corners.push_back(
                {written.a, solved.a, "point " + m_pointItems[static_cast<std::size_t>(startPoint(data, step))]->name});
            if (step.arc) {
                corners.push_back({written.m, solved.m, "the middle of arc " + step.item->name});
            }
        }
        const auto turn = [](kernel::Point2 p, kernel::Point2 q, kernel::Point2 r) {
            const double ux = q.u - p.u;
            const double uy = q.v - p.v;
            const double vx = r.u - q.u;
            const double vy = r.v - q.v;
            const double scale = std::hypot(ux, uy) * std::hypot(vx, vy);
            const double cross = ux * vy - uy * vx;
            return scale > 0.0 && std::abs(cross) > 1e-6 * scale ? (cross > 0.0 ? 1 : -1) : 0;
        };
        const std::size_t n = corners.size();
        for (std::size_t i = 0; i < n && n >= 3; ++i) {
            const Corner& p = corners[(i + n - 1) % n];
            const Corner& q = corners[i];
            const Corner& r = corners[(i + 1) % n];
            const int written = turn(p.written, q.written, r.written);
            const int solved = turn(p.solved, q.solved, r.solved);
            if (written != 0 && solved != 0 && written != solved) {
                return Trouble{"flip", "Sketch '" + m_sketch.name + "' solved to a shape that turns the other way at " +
                                           q.name + " than its guesses do"};
            }
        }
        return std::nullopt;
    }

    std::string fixFor(const Trouble& wrong) const
    {
        for (std::size_t i = 0; i < m_arcItems.size(); ++i) {
            solve::Sketch turned = m_data;
            std::swap(turned.arcs[i].start, turned.arcs[i].end);
            const std::optional<std::vector<solve::Point>> points = solve::positions(turned);
            if (!points) {
                continue;
            }
            bool clean = true;
            for (const std::vector<Step>& walk : walks(turned)) {
                clean = clean && !trouble(turned, *points, walk);
            }
            if (clean) {
                const SketchItem& arc = *m_arcItems[i];
                return "Arcs run counterclockwise from start to end. Write: arc " + arc.name + " from " + arc.refs[1] +
                       " to " + arc.refs[0] + " center " + arc.refs[2];
            }
        }
        if (wrong.code != "flip") {
            return "A loop cannot turn back on itself. Check the tangents and the direction of the arcs there";
        }
        std::string lines;
        for (std::size_t i = 0; i < m_pointItems.size(); ++i) {
            const SketchItem& item = *m_pointItems[i];
            const std::string solved = position(m_solution.points[i]);
            if (item.center.x &&
                solved != "(" + lang::format(*item.center.x) + ", " + lang::format(*item.center.y) + ")") {
                lines += "\n  point " + item.name + " near " + solved;
            }
        }
        return "If the solved shape is the one you meant, write these guesses:" + lines +
               "\nOtherwise add a constraint that rules the solved shape out";
    }

    std::vector<std::vector<Step>> walks(const solve::Sketch& data) const
    {
        std::vector<int> root(data.points.size());
        std::iota(root.begin(), root.end(), 0);
        const auto find = [&](int p) {
            while (root[static_cast<std::size_t>(p)] != p) {
                p = root[static_cast<std::size_t>(p)];
            }
            return p;
        };
        for (const solve::Constraint& c : data.constraints) {
            if (c.kind == solve::Constraint::Kind::Coincident && c.refs.size() == 2 &&
                c.refs[1].kind == solve::Ref::Kind::Point) {
                root[static_cast<std::size_t>(find(c.refs[0].index))] = find(c.refs[1].index);
            }
        }
        struct Edge {
            Step step;
            int a;
            int b;
        };
        std::vector<Edge> edges;
        int line = 0;
        int arc = 0;
        for (const SketchItem& item : m_sketch.items) {
            if (item.kind == SketchItem::Kind::Line) {
                const solve::Line& l = data.lines[static_cast<std::size_t>(line)];
                edges.push_back({{&item, false, line++, true}, find(l.a), find(l.b)});
            } else if (item.kind == SketchItem::Kind::Arc) {
                const solve::Arc& a = data.arcs[static_cast<std::size_t>(arc)];
                edges.push_back({{&item, true, arc++, true}, find(a.start), find(a.end)});
            }
        }
        std::map<int, std::vector<std::size_t>> at;
        for (std::size_t i = 0; i < edges.size(); ++i) {
            at[edges[i].a].push_back(i);
            at[edges[i].b].push_back(i);
        }
        for (const auto& [vertex, touching] : at) {
            const std::string name = m_pointItems[static_cast<std::size_t>(vertex)]->name;
            if (touching.size() == 1) {
                const SketchItem& item = *edges[touching.front()].step.item;
                throw EvalError(item.span,
                                "Sketch '" + m_sketch.name + "' has a loop left open at point " + name + ", where '" +
                                    item.name + "' ends and nothing else does",
                                "Start or end another line or arc at " + name + " to close the loop", "open");
            }
            if (touching.size() > 2) {
                std::vector<std::string> names;
                names.reserve(touching.size());
                for (const std::size_t i : touching) {
                    names.push_back(edges[i].step.item->name);
                }
                throw EvalError(edges[touching.front()].step.item->span,
                                "Sketch '" + m_sketch.name + "' meets at point " + name + " with " + listed(names),
                                "A loop meets two lines or arcs at each point. Split this into separate loops",
                                "junction");
            }
        }
        std::vector<bool> used(edges.size(), false);
        std::vector<std::vector<Step>> out;
        for (std::size_t first = 0; first < edges.size(); ++first) {
            if (used[first]) {
                continue;
            }
            std::vector<Step> walk;
            std::size_t current = first;
            int from = edges[first].a;
            while (!used[current]) {
                used[current] = true;
                Step step = edges[current].step;
                step.forward = edges[current].a == from;
                walk.push_back(step);
                from = step.forward ? edges[current].b : edges[current].a;
                for (const std::size_t next : at[from]) {
                    if (!used[next]) {
                        current = next;
                    }
                }
            }
            out.push_back(std::move(walk));
        }
        return out;
    }

    const Statement& m_sketch;
    const lang::Params& m_params;
    solve::Sketch m_data;
    solve::Solution m_solution;
    std::map<std::string, solve::Ref> m_refs;
    std::vector<const SketchItem*> m_pointItems;
    std::vector<const SketchItem*> m_arcItems;
    std::vector<const SketchItem*> m_circleItems;
    std::vector<const SketchItem*> m_sources;
};

} // namespace

double lengthOf(const lang::Expr& expr, const lang::Params& params, const std::string& what, bool positive)
{
    const lang::Quantity q = lang::evaluate(expr, params);
    if (positive && q.value <= 0.0) {
        throw EvalError(expr.span, "The " + what + " must be above zero, and it is " + lang::toText(q));
    }
    return q.value;
}

std::vector<Outline> sketchOutlines(const Statement& sketch, const lang::Params& params,
                                    std::vector<Diagnostic>& warnings, std::vector<model::SolvedSketch>& solvedSketches)
{
    std::vector<Outline> out;
    for (const SketchItem& item : sketch.items) {
        if (item.kind == SketchItem::Kind::Rect || (item.kind == SketchItem::Kind::Circle && item.refs.empty())) {
            out.push_back(writtenOutline(item, params));
        }
    }
    Solved solved(sketch, params);
    if (!solved.empty()) {
        solved.run(warnings);
        solvedSketches.push_back(solved.report());
        std::vector<Outline> loops = solved.loops();
        out.insert(out.end(), std::make_move_iterator(loops.begin()), std::make_move_iterator(loops.end()));
    }
    return out;
}

} // namespace kalve137::rebuild
