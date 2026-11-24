// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/offset.h"

#include "sketch/select.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace kalve137::sketch {

namespace {

constexpr double kTiny = 1e-9;
constexpr double kTangentCross = 1e-6;

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
    return n > kTiny ? a * (1.0 / n) : Vec2{};
}

Vec2 leftOf(Vec2 a)
{
    return {-a.y, a.x};
}

class Joints {
public:
    explicit Joints(const Sketch& sketch)
    {
        for (const auto& [id, c] : sketch.constraints()) {
            if (c.kind != ConstraintKind::Coincident || c.refs.size() != 2 || !sketch.isPoint(c.refs[0]) ||
                !sketch.isPoint(c.refs[1])) {
                continue;
            }
            const Id a = find(c.refs[0]);
            const Id b = find(c.refs[1]);
            if (a != b) {
                m_parent[a] = b;
            }
        }
    }

    Id find(Id id) const
    {
        for (auto it = m_parent.find(id); it != m_parent.end(); it = m_parent.find(id)) {
            id = it->second;
        }
        return id;
    }

private:
    std::map<Id, Id> m_parent;
};

std::optional<std::pair<Id, Id>> endsOf(const Curve& curve)
{
    switch (curve.kind) {
    case CurveKind::Line:
        return std::pair{curve.points[0], curve.points[1]};
    case CurveKind::Arc:
        return std::pair{curve.points[1], curve.points[2]};
    case CurveKind::Circle:
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<Vec2> traversed(const Sketch& sketch, Id id, bool reversed)
{
    std::vector<Vec2> points = polyline(sketch, *sketch.curve(id));
    if (reversed) {
        std::ranges::reverse(points);
    }
    return points;
}

double signedArea(const Sketch& sketch, const Chain& chain)
{
    std::vector<Vec2> loop;
    for (std::size_t i = 0; i < chain.curves.size(); ++i) {
        const std::vector<Vec2> points = traversed(sketch, chain.curves[i], chain.reversed[i]);
        loop.insert(loop.end(), points.begin(), points.end());
    }
    double area = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        area += cross(loop[i], loop[(i + 1) % loop.size()]);
    }
    return area / 2.0;
}

struct Piece {
    CurveKind kind = CurveKind::Line;
    Id source = 0;
    bool reversed = false;
    bool construction = false;
    Vec2 start;
    Vec2 end;
    Vec2 center;
    double radius = 0.0;
    Vec2 sourceStart;
    Vec2 sourceEnd;
};

std::optional<Piece> pieceOf(const Sketch& sketch, Id id, bool reversed, double amount)
{
    const Curve& curve = *sketch.curve(id);
    const auto at = [&](std::size_t i) { return sketch.point(curve.points[i])->pos; };
    Piece piece{.kind = curve.kind, .source = id, .reversed = reversed, .construction = curve.construction};
    switch (curve.kind) {
    case CurveKind::Line: {
        piece.sourceStart = at(reversed ? 1 : 0);
        piece.sourceEnd = at(reversed ? 0 : 1);
        const Vec2 shift = leftOf(unit(piece.sourceEnd - piece.sourceStart)) * amount;
        piece.start = piece.sourceStart + shift;
        piece.end = piece.sourceEnd + shift;
        return piece;
    }
    case CurveKind::Arc: {
        piece.center = at(0);
        piece.sourceStart = at(reversed ? 2 : 1);
        piece.sourceEnd = at(reversed ? 1 : 2);
        piece.radius = reversed ? curve.radius + amount : curve.radius - amount;
        if (piece.radius <= kTiny) {
            return std::nullopt;
        }
        piece.start = piece.center + unit(piece.sourceStart - piece.center) * piece.radius;
        piece.end = piece.center + unit(piece.sourceEnd - piece.center) * piece.radius;
        return piece;
    }
    case CurveKind::Circle:
        piece.center = at(0);
        piece.radius = curve.radius + amount;
        if (piece.radius <= kTiny) {
            return std::nullopt;
        }
        return piece;
    }
    return std::nullopt;
}

std::vector<Vec2> lineCircle(Vec2 p, Vec2 dir, Vec2 center, double radius, double tolerance)
{
    const Vec2 u = unit(dir);
    const Vec2 w = p - center;
    const double b = dot(w, u);
    double disc = b * b - (dot(w, w) - radius * radius);
    if (disc < 0.0) {
        if (disc < -tolerance * radius) {
            return {};
        }
        disc = 0.0;
    }
    const double root = std::sqrt(disc);
    return {p + u * (-b + root), p + u * (-b - root)};
}

std::vector<Vec2> circleCircle(Vec2 c0, double r0, Vec2 c1, double r1)
{
    const double d = distance(c0, c1);
    if (d < kTiny || d > r0 + r1 || d < std::abs(r0 - r1)) {
        return {};
    }
    const double a = (r0 * r0 - r1 * r1 + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, r0 * r0 - a * a));
    const Vec2 u = unit(c1 - c0);
    const Vec2 mid = c0 + u * a;
    return {mid + leftOf(u) * h, mid - leftOf(u) * h};
}

std::optional<Vec2> meet(const Piece& a, const Piece& b, double scale)
{
    const Vec2 guess = (a.end + b.start) * 0.5;
    if (distance(a.end, b.start) <= kTiny * scale) {
        return guess;
    }
    std::vector<Vec2> candidates;
    if (a.kind == CurveKind::Line && b.kind == CurveKind::Line) {
        const Vec2 r = a.end - a.start;
        const Vec2 s = b.end - b.start;
        const double denominator = cross(r, s);
        if (std::abs(denominator) > kTiny * norm(r) * norm(s)) {
            candidates.push_back(a.start + r * (cross(b.start - a.start, s) / denominator));
        }
    } else if (a.kind == CurveKind::Line) {
        candidates = lineCircle(a.start, a.end - a.start, b.center, b.radius, kTangentCross);
    } else if (b.kind == CurveKind::Line) {
        candidates = lineCircle(b.start, b.end - b.start, a.center, a.radius, kTangentCross);
    } else {
        candidates = circleCircle(a.center, a.radius, b.center, b.radius);
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    return *std::ranges::min_element(candidates,
                                     [&](Vec2 p, Vec2 q) { return distance(p, guess) < distance(q, guess); });
}

double chainScale(const Sketch& sketch, const Chain& chain)
{
    double scale = 0.0;
    for (const Id id : chain.curves) {
        for (const Vec2 p : polyline(sketch, *sketch.curve(id))) {
            scale = std::max({scale, std::abs(p.x), std::abs(p.y)});
        }
    }
    return std::max(scale, 1.0);
}

} // namespace

std::optional<Chain> orderChain(const Sketch& sketch, const std::vector<Id>& curves)
{
    if (curves.empty()) {
        return std::nullopt;
    }
    for (const Id id : curves) {
        if (sketch.curve(id) == nullptr) {
            return std::nullopt;
        }
    }
    if (curves.size() == 1 && sketch.curve(curves[0])->kind == CurveKind::Circle) {
        return Chain{.curves = curves, .reversed = {false}, .closed = true};
    }
    const Joints joints(sketch);
    std::map<Id, std::vector<Id>> at;
    std::map<Id, std::pair<Id, Id>> endsById;
    for (const Id id : curves) {
        const std::optional<std::pair<Id, Id>> ends = endsOf(*sketch.curve(id));
        if (!ends) {
            return std::nullopt;
        }
        endsById[id] = *ends;
        at[joints.find(ends->first)].push_back(id);
        at[joints.find(ends->second)].push_back(id);
    }
    Id start = joints.find(endsById.at(curves[0]).first);
    bool open = false;
    for (const auto& [joint, touching] : at) {
        if (touching.size() > 2) {
            return std::nullopt;
        }
        if (touching.size() == 1 && !open) {
            open = true;
            start = joint;
        }
    }
    Chain chain;
    std::set<Id> used;
    Id joint = start;
    while (true) {
        const std::vector<Id>& touching = at[joint];
        const auto next = std::ranges::find_if(touching, [&](Id id) { return !used.contains(id); });
        if (next == touching.end()) {
            break;
        }
        const Id id = *next;
        used.insert(id);
        const std::pair<Id, Id> ends = endsById.at(id);
        const bool reversed = joints.find(ends.first) != joint;
        chain.curves.push_back(id);
        chain.reversed.push_back(reversed);
        joint = joints.find(reversed ? ends.first : ends.second);
    }
    if (used.size() != curves.size()) {
        return std::nullopt;
    }
    chain.closed = !open && joint == start && chain.curves.size() > 1;
    if (chain.closed && signedArea(sketch, chain) > 0.0) {
        std::ranges::reverse(chain.curves);
        std::ranges::reverse(chain.reversed);
        for (std::size_t i = 0; i < chain.reversed.size(); ++i) {
            chain.reversed[i] = !chain.reversed[i];
        }
    }
    return chain;
}

std::vector<Id> chainThrough(const Sketch& sketch, Id curve)
{
    const Curve* picked = sketch.curve(curve);
    if (picked == nullptr) {
        return {};
    }
    const std::optional<std::pair<Id, Id>> pickedEnds = endsOf(*picked);
    if (!pickedEnds) {
        return {curve};
    }
    const Joints joints(sketch);
    std::map<Id, std::vector<Id>> at;
    std::map<Id, std::pair<Id, Id>> endsById;
    for (const auto& [id, c] : sketch.curves()) {
        if (c.construction != picked->construction) {
            continue;
        }
        if (const std::optional<std::pair<Id, Id>> ends = endsOf(c)) {
            endsById[id] = *ends;
            at[joints.find(ends->first)].push_back(id);
            at[joints.find(ends->second)].push_back(id);
        }
    }
    std::vector<Id> chain{curve};
    std::set<Id> seen{curve};
    for (const Id end : {pickedEnds->first, pickedEnds->second}) {
        Id joint = joints.find(end);
        Id previous = curve;
        while (true) {
            const std::vector<Id>& touching = at[joint];
            if (touching.size() != 2) {
                break;
            }
            const Id next = touching[0] == previous ? touching[1] : touching[0];
            if (seen.contains(next)) {
                break;
            }
            seen.insert(next);
            chain.push_back(next);
            const std::pair<Id, Id> ends = endsById.at(next);
            joint = joints.find(ends.first) == joint ? joints.find(ends.second) : joints.find(ends.first);
            previous = next;
        }
    }
    return chain;
}

int offsetSide(const Sketch& sketch, const Chain& chain, Vec2 cursor)
{
    double best = std::numeric_limits<double>::max();
    int side = 0;
    for (std::size_t i = 0; i < chain.curves.size(); ++i) {
        const std::vector<Vec2> points = traversed(sketch, chain.curves[i], chain.reversed[i]);
        for (std::size_t k = 0; k + 1 < points.size(); ++k) {
            const Vec2 a = points[k];
            const Vec2 ab = points[k + 1] - a;
            const double length2 = dot(ab, ab);
            const double t = length2 > 0.0 ? std::clamp(dot(cursor - a, ab) / length2, 0.0, 1.0) : 0.0;
            const double gap = distance(cursor, a + ab * t);
            if (gap < best) {
                best = gap;
                const double turn = cross(ab, cursor - a);
                side = turn > 0.0 ? 1 : turn < 0.0 ? -1 : 0;
            }
        }
    }
    const Curve& only = *sketch.curve(chain.curves[0]);
    if (chain.curves.size() == 1 && only.kind == CurveKind::Circle) {
        const double gap = distance(cursor, sketch.point(only.points[0])->pos) - only.radius;
        return gap > 0.0 ? 1 : gap < 0.0 ? -1 : 0;
    }
    return side;
}

std::optional<Offset> addOffset(Sketch& sketch, const std::vector<Id>& curves, double amount, bool linked)
{
    const std::optional<Chain> chain = orderChain(sketch, curves);
    if (!chain || std::abs(amount) <= kTiny) {
        return std::nullopt;
    }
    const double scale = chainScale(sketch, *chain);
    const std::size_t count = chain->curves.size();
    if (count == 0) {
        return std::nullopt;
    }
    std::vector<Piece> pieces;
    for (std::size_t i = 0; i < count; ++i) {
        std::optional<Piece> piece = pieceOf(sketch, chain->curves[i], chain->reversed[i], amount);
        if (!piece) {
            return std::nullopt;
        }
        pieces.push_back(*piece);
    }
    const bool circle = pieces[0].kind == CurveKind::Circle;
    std::vector<Vec2> joints;
    if (!circle) {
        const std::size_t junctions = chain->closed ? count : count - 1;
        if (!chain->closed) {
            joints.push_back(pieces.front().start);
        }
        for (std::size_t i = 0; i < junctions; ++i) {
            const Piece& a = pieces[i];
            const Piece& b = pieces[(i + 1) % count];
            const std::optional<Vec2> joint = meet(a, b, scale);
            if (!joint) {
                return std::nullopt;
            }
            joints.push_back(*joint);
        }
        if (chain->closed) {
            std::ranges::rotate(joints, joints.end() - 1);
        } else {
            joints.push_back(pieces.back().end);
        }
    }
    const auto jointAt = [&](std::size_t i) { return joints[i % joints.size()]; };
    for (std::size_t i = 0; i < count && !circle; ++i) {
        const Vec2 from = jointAt(i);
        const Vec2 to = jointAt(i + 1);
        if (distance(from, to) <= kTiny * scale) {
            return std::nullopt;
        }
        if (pieces[i].kind == CurveKind::Line && dot(to - from, pieces[i].sourceEnd - pieces[i].sourceStart) <= 0.0) {
            return std::nullopt;
        }
    }

    Sketch built = sketch;
    Offset offset;
    std::vector<Id> jointIds;
    jointIds.reserve(joints.size());
    for (const Vec2 joint : joints) {
        jointIds.push_back(built.addPoint(joint));
    }
    const auto jointId = [&](std::size_t i) { return jointIds[i % jointIds.size()]; };
    for (std::size_t i = 0; i < count; ++i) {
        const Piece& piece = pieces[i];
        const Curve& source = *built.curve(piece.source);
        const Id first = circle ? 0 : jointId(piece.reversed ? i + 1 : i);
        const Id second = circle ? 0 : jointId(piece.reversed ? i : i + 1);
        switch (piece.kind) {
        case CurveKind::Line:
            offset.curves.push_back(built.addLine(first, second, piece.construction));
            break;
        case CurveKind::Arc:
            offset.curves.push_back(built.addArc(source.points[0], first, second, piece.construction));
            built.curve(offset.curves.back())->radius = piece.radius;
            break;
        case CurveKind::Circle:
            offset.curves.push_back(built.addCircle(source.points[0], piece.radius, piece.construction));
            break;
        }
    }
    if (linked) {
        const int side = amount > 0.0 ? 1 : -1;
        std::vector<OffsetPair> pairs;
        for (std::size_t i = 0; i < count; ++i) {
            const Piece& piece = pieces[i];
            const int along = piece.reversed ? -side : side;
            const int pairSide = piece.kind == CurveKind::Line ? along : piece.kind == CurveKind::Arc ? -along : side;
            pairs.push_back({piece.source, offset.curves[i], pairSide});
        }
        offset.constraint = built.nextId();
        built.insertConstraint(offset.constraint, offsetConstraint(pairs, std::abs(amount)));
    }
    sketch = std::move(built);
    return offset;
}

} // namespace kalve137::sketch
