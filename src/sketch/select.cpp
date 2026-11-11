// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/select.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::sketch {

namespace {

constexpr int kCircleSegments = 64;
constexpr int kMinArcSegments = 8;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct Box {
    Vec2 low;
    Vec2 high;

    bool contains(Vec2 p) const { return p.x >= low.x && p.x <= high.x && p.y >= low.y && p.y <= high.y; }

    bool crosses(Vec2 a, Vec2 b) const
    {
        if (contains(a) || contains(b)) {
            return true;
        }
        double enter = 0.0;
        double leave = 1.0;
        const Vec2 d = b - a;
        const auto clip = [&](double p, double q) {
            if (std::abs(p) < 1e-12) {
                return q >= 0.0;
            }
            const double t = q / p;
            if (p < 0.0) {
                enter = std::max(enter, t);
            } else {
                leave = std::min(leave, t);
            }
            return enter <= leave;
        };
        return clip(-d.x, a.x - low.x) && clip(d.x, high.x - a.x) && clip(-d.y, a.y - low.y) && clip(d.y, high.y - a.y);
    }
};

std::vector<Vec2> arcPoints(Vec2 center, double radius, double start, double sweep, int segments)
{
    std::vector<Vec2> points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = start + sweep * i / segments;
        points.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
    }
    return points;
}

} // namespace

std::vector<Vec2> polyline(const Sketch& sketch, const Curve& curve)
{
    std::vector<Vec2> ends;
    for (const Id id : curve.points) {
        const Point* point = sketch.point(id);
        if (point == nullptr) {
            return {};
        }
        ends.push_back(point->pos);
    }
    switch (curve.kind) {
    case CurveKind::Line:
        return ends.size() == 2 ? ends : std::vector<Vec2>{};
    case CurveKind::Circle:
        return ends.empty() ? std::vector<Vec2>{} : arcPoints(ends[0], curve.radius, 0.0, kTwoPi, kCircleSegments);
    case CurveKind::Arc: {
        if (ends.size() != 3) {
            return {};
        }
        const double start = std::atan2(ends[1].y - ends[0].y, ends[1].x - ends[0].x);
        double end = std::atan2(ends[2].y - ends[0].y, ends[2].x - ends[0].x);
        if (end <= start) {
            end += kTwoPi;
        }
        const double sweep = end - start;
        const int segments = std::max(kMinArcSegments, static_cast<int>(std::ceil(kCircleSegments * sweep / kTwoPi)));
        return arcPoints(ends[0], curve.radius, start, sweep, segments);
    }
    }
    return {};
}

std::vector<Id> inBox(const Sketch& sketch, const Projection& project, Vec2 corner, Vec2 opposite, BoxMode mode)
{
    const Box box{{std::min(corner.x, opposite.x), std::min(corner.y, opposite.y)},
                  {std::max(corner.x, opposite.x), std::max(corner.y, opposite.y)}};
    std::vector<Id> ids;
    for (const auto& [id, point] : sketch.points()) {
        if (id != kOriginId && box.contains(project(point.pos))) {
            ids.push_back(id);
        }
    }
    for (const auto& [id, curve] : sketch.curves()) {
        std::vector<Vec2> points = polyline(sketch, curve);
        if (points.empty()) {
            continue;
        }
        std::ranges::transform(points, points.begin(), project);
        bool inside = false;
        if (mode == BoxMode::Window) {
            inside = std::ranges::all_of(points, [&](Vec2 p) { return box.contains(p); });
        } else {
            for (std::size_t i = 0; i + 1 < points.size() && !inside; ++i) {
                inside = box.crosses(points[i], points[i + 1]);
            }
        }
        if (inside) {
            ids.push_back(id);
        }
    }
    return ids;
}

} // namespace kalve137::sketch
