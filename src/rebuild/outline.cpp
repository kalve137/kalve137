// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rebuild/outline.h"

#include <cmath>
#include <numbers>

namespace kalve137::rebuild {

namespace {

constexpr int kArcSteps = 24;
constexpr int kCircleSteps = 72;

using kernel::Point2;
using kernel::Segment;

void arcPoints(const Segment& s, std::vector<Point2>& out)
{
    const double d = 2.0 * (s.a.u * (s.m.v - s.b.v) + s.m.u * (s.b.v - s.a.v) + s.b.u * (s.a.v - s.m.v));
    if (std::abs(d) < 1e-12) {
        out.push_back(s.a);
        return;
    }
    const auto sq = [](Point2 p) { return p.u * p.u + p.v * p.v; };
    const Point2 c{(sq(s.a) * (s.m.v - s.b.v) + sq(s.m) * (s.b.v - s.a.v) + sq(s.b) * (s.a.v - s.m.v)) / d,
                   (sq(s.a) * (s.b.u - s.m.u) + sq(s.m) * (s.a.u - s.b.u) + sq(s.b) * (s.m.u - s.a.u)) / d};
    const bool left = (s.m.u - s.a.u) * (s.b.v - s.a.v) - (s.m.v - s.a.v) * (s.b.u - s.a.u) > 0.0;
    const double start = std::atan2(s.a.v - c.v, s.a.u - c.u);
    const double end = std::atan2(s.b.v - c.v, s.b.u - c.u);
    double sweep = left ? end - start : start - end;
    while (sweep <= 0.0) {
        sweep += 2.0 * std::numbers::pi;
    }
    const double radius = std::hypot(s.a.u - c.u, s.a.v - c.v);
    for (int i = 0; i < kArcSteps; ++i) {
        const double angle = start + (left ? 1.0 : -1.0) * sweep * i / kArcSteps;
        out.push_back({c.u + radius * std::cos(angle), c.v + radius * std::sin(angle)});
    }
}

bool contains(const std::vector<Point2>& polygon, Point2 p)
{
    bool in = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point2 a = polygon[i];
        const Point2 b = polygon[j];
        if ((a.v > p.v) != (b.v > p.v) && p.u < (b.u - a.u) * (p.v - a.v) / (b.v - a.v) + a.u) {
            in = !in;
        }
    }
    return in;
}

} // namespace

std::vector<Point2> polygon(const kernel::Loop& loop)
{
    std::vector<Point2> points;
    for (const Segment& s : loop) {
        switch (s.kind) {
        case Segment::Kind::Line:
            points.push_back(s.a);
            break;
        case Segment::Kind::Arc:
            arcPoints(s, points);
            break;
        case Segment::Kind::Circle:
            for (int i = 0; i < kCircleSteps; ++i) {
                const double angle = 2.0 * std::numbers::pi * i / kCircleSteps;
                points.push_back({s.c.u + s.radius * std::cos(angle), s.c.v + s.radius * std::sin(angle)});
            }
            break;
        }
    }
    return points;
}

double enclosedArea(const kernel::Loop& loop)
{
    if (loop.size() == 1 && loop.front().kind == Segment::Kind::Circle) {
        return std::numbers::pi * loop.front().radius * loop.front().radius;
    }
    const std::vector<Point2> points = polygon(loop);
    double sum = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point2 p = points[i];
        const Point2 q = points[(i + 1) % points.size()];
        sum += p.u * q.v - q.u * p.v;
    }
    return std::abs(sum) / 2.0;
}

Point2 interiorPoint(const kernel::Loop& loop)
{
    if (loop.size() == 1 && loop.front().kind == Segment::Kind::Circle) {
        return loop.front().c;
    }
    const std::vector<Point2> points = polygon(loop);
    Point2 mean;
    for (const Point2& p : points) {
        mean.u += p.u / static_cast<double>(points.size());
        mean.v += p.v / static_cast<double>(points.size());
    }
    if (contains(points, mean)) {
        return mean;
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point2 a = points[i];
        const Point2 b = points[(i + 1) % points.size()];
        const Point2 c = points[(i + 2) % points.size()];
        const Point2 trial{(a.u + b.u + c.u) / 3.0, (a.v + b.v + c.v) / 3.0};
        if (contains(points, trial)) {
            return trial;
        }
    }
    return mean;
}

bool inside(const kernel::Loop& outer, const kernel::Loop& inner)
{
    return enclosedArea(outer) > enclosedArea(inner) && contains(polygon(outer), interiorPoint(inner));
}

} // namespace kalve137::rebuild
