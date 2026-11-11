// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/draft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::sketch {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kTiny = 1e-12;

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
    return n > kTiny ? Vec2{a.x / n, a.y / n} : Vec2{1.0, 0.0};
}

Vec2 rotated(Vec2 a, double radians)
{
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return {a.x * c - a.y * s, a.x * s + a.y * c};
}

} // namespace

Vec2 lineEnd(Vec2 start, Vec2 cursor, std::optional<Vec2> previousDir, std::optional<double> length,
             std::optional<double> interiorDegrees)
{
    const Vec2 steer{cursor.x - start.x, cursor.y - start.y};
    Vec2 dir = norm(steer) > kTiny ? unit(steer) : (previousDir ? unit(*previousDir) : Vec2{1.0, 0.0});
    if (interiorDegrees && previousDir) {
        const Vec2 back = unit(Vec2{-previousDir->x, -previousDir->y});
        const double side = cross(back, dir) >= 0.0 ? 1.0 : -1.0;
        dir = rotated(back, side * *interiorDegrees * kDegToRad);
    }
    const double size =
        length ? *length
               : (interiorDegrees && previousDir ? std::max(0.0, steer.x * dir.x + steer.y * dir.y) : norm(steer));
    return {start.x + dir.x * size, start.y + dir.y * size};
}

double interiorAngle(Vec2 previousDir, Vec2 dir)
{
    const Vec2 back{-previousDir.x, -previousDir.y};
    return std::abs(std::atan2(cross(back, dir), back.x * dir.x + back.y * dir.y)) * kRadToDeg;
}

Vec2 snapToGrid(Vec2 pos, double step)
{
    if (!(step > 0.0)) {
        return pos;
    }
    return {std::round(pos.x / step) * step, std::round(pos.y / step) * step};
}

} // namespace kalve137::sketch
