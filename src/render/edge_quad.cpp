// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/edge_quad.h"

#include <cmath>

namespace kalve137::render {

namespace {

constexpr double kNearW = 1e-6;
constexpr double kShortest = 1e-9;

Clip mixed(const Clip& a, const Clip& b, double t)
{
    return {a[0] + ((b[0] - a[0]) * t), a[1] + ((b[1] - a[1]) * t), a[2] + ((b[2] - a[2]) * t),
            a[3] + ((b[3] - a[3]) * t)};
}

} // namespace

std::optional<Clip> expandEdge(const Clip& start, const Clip& end, int corner, double widthPixels, double width,
                               double height, Cap cap)
{
    if (start[3] < kNearW && end[3] < kNearW) {
        return std::nullopt;
    }
    Clip a = start;
    Clip b = end;
    if (a[3] < kNearW) {
        a = mixed(a, b, (kNearW - a[3]) / (b[3] - a[3]));
    } else if (b[3] < kNearW) {
        b = mixed(b, a, (kNearW - b[3]) / (a[3] - b[3]));
    }
    double dx = ((b[0] / b[3]) - (a[0] / a[3])) * width * 0.5;
    double dy = ((b[1] / b[3]) - (a[1] / a[3])) * height * 0.5;
    const double length = std::sqrt((dx * dx) + (dy * dy));
    if (length > kShortest) {
        dx /= length;
        dy /= length;
    } else {
        dx = 1.0;
        dy = 0.0;
    }
    const bool atEnd = corner >= 2;
    const double side = corner % 2 == 0 ? -1.0 : 1.0;
    const double along = cap == Cap::Butt ? 0.0 : (atEnd ? 1.0 : -1.0);
    const double half = widthPixels * 0.5;
    const double offsetX = ((-dy * side) + (dx * along)) * half;
    const double offsetY = ((dx * side) + (dy * along)) * half;
    Clip result = atEnd ? b : a;
    result[0] += offsetX / (width * 0.5) * result[3];
    result[1] += offsetY / (height * 0.5) * result[3];
    return result;
}

} // namespace kalve137::render
