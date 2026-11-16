// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>
#include <optional>
#include <string>

namespace kalve137::model {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(Vec3 a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

inline double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalized(Vec3 v)
{
    const double length = std::sqrt(dot(v, v));
    return length > 0.0 ? v * (1.0 / length) : v;
}

struct Frame {
    Vec3 origin;
    Vec3 normal{0, 0, 1};
    Vec3 x{1, 0, 0};
    Vec3 y{0, 1, 0};
    bool datum = true;
};

inline Frame planeFrame(Vec3 normal, Vec3 pointOnPlane)
{
    Frame frame;
    frame.normal = normal;
    frame.origin = normal * dot(pointOnPlane, normal);
    frame.x = std::abs(normal.z) > 0.999 ? Vec3{1, 0, 0} : normalized(cross({0, 0, 1}, normal));
    frame.y = cross(normal, frame.x);
    frame.datum = false;
    return frame;
}

inline std::optional<Frame> datumFrame(const std::string& name)
{
    if (name == "XY") {
        return Frame{{}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, true};
    }
    if (name == "XZ") {
        return Frame{{}, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, true};
    }
    if (name == "YZ") {
        return Frame{{}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, true};
    }
    return std::nullopt;
}

inline Vec3 at(const Frame& frame, double u, double v)
{
    return frame.origin + frame.x * u + frame.y * v;
}

} // namespace kalve137::model
