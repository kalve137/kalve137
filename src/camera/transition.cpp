// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "camera/transition.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kalve137::camera {

namespace {

constexpr double kConfusion = 1e-7;
constexpr double kResolution = std::numeric_limits<double>::min();

struct Quaternion {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;

    friend Quaternion operator*(const Quaternion& a, const Quaternion& b)
    {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
                a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }
    friend Quaternion operator+(const Quaternion& a, const Quaternion& b)
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    }
    friend Quaternion operator-(const Quaternion& a, const Quaternion& b)
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    }
    friend Quaternion operator*(const Quaternion& a, double s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }

    double dot(const Quaternion& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }
    double norm() const { return std::sqrt(dot(*this)); }
    Quaternion normalized() const { return *this * (1.0 / norm()); }
    Quaternion inverted() const { return Quaternion{-x, -y, -z, w} * (1.0 / dot(*this)); }
};

double element(const Mat3& m, int row, int column)
{
    const Vec3& c = m.columns[static_cast<std::size_t>(column - 1)];
    return row == 1 ? c.x : (row == 2 ? c.y : c.z);
}

Quaternion fromMatrix(const Mat3& m)
{
    const auto at = [&m](int row, int column) { return element(m, row, column); };
    const double trace = at(1, 1) + at(2, 2) + at(3, 3);
    if (trace > 0.0) {
        const Quaternion q{at(3, 2) - at(2, 3), at(1, 3) - at(3, 1), at(2, 1) - at(1, 2), trace + 1.0};
        return q * (0.5 / std::sqrt(q.w));
    }
    if (at(1, 1) > at(2, 2) && at(1, 1) > at(3, 3)) {
        const Quaternion q{1.0 + at(1, 1) - at(2, 2) - at(3, 3), at(1, 2) + at(2, 1), at(1, 3) + at(3, 1),
                           at(3, 2) - at(2, 3)};
        return q * (0.5 / std::sqrt(q.x));
    }
    if (at(2, 2) > at(3, 3)) {
        const Quaternion q{at(1, 2) + at(2, 1), 1.0 + at(2, 2) - at(1, 1) - at(3, 3), at(2, 3) + at(3, 2),
                           at(1, 3) - at(3, 1)};
        return q * (0.5 / std::sqrt(q.y));
    }
    const Quaternion q{at(1, 3) + at(3, 1), at(2, 3) + at(3, 2), 1.0 + at(3, 3) - at(1, 1) - at(2, 2),
                       at(2, 1) - at(1, 2)};
    return q * (0.5 / std::sqrt(q.z));
}

Mat3 toMatrix(const Quaternion& rotation)
{
    const Quaternion q = rotation.normalized();
    const double xx = q.x * q.x;
    const double yy = q.y * q.y;
    const double zz = q.z * q.z;
    const double xy = q.x * q.y;
    const double xz = q.x * q.z;
    const double yz = q.y * q.z;
    const double wx = q.w * q.x;
    const double wy = q.w * q.y;
    const double wz = q.w * q.z;
    return {{Vec3{1.0 - 2.0 * (yy + zz), 2.0 * (xy + wz), 2.0 * (xz - wy)},
             Vec3{2.0 * (xy - wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz + wx)},
             Vec3{2.0 * (xz + wy), 2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy)}}};
}

Mat3 axes(const Camera& camera)
{
    const Vec3 back = -camera.direction();
    const Vec3 x = normalized(cross(camera.up(), back));
    const Vec3 y = normalized(cross(back, x));
    const Vec3 z = normalized(cross(x, y));
    return {{x, y, z}};
}

Vec3 lerp(const Vec3& a, const Vec3& b, double t)
{
    return a + (b - a) * t;
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

} // namespace

Camera interpolate(const Camera& start, const Camera& end, double t)
{
    if (std::abs(t - 1.0) < kConfusion) {
        return end;
    }
    Camera camera = start;
    if (std::abs(t) < kConfusion) {
        return camera;
    }

    const Quaternion delta = fromMatrix(axes(end)) * fromMatrix(axes(start)).inverted();
    const Quaternion identity;
    Quaternion target = delta.normalized();
    if (identity.dot(target) < 0.0) {
        target = target * -1.0;
    }
    camera.transform(Motion{toMatrix(identity + (target - identity) * t), {}});

    const Vec3 center = lerp(start.center(), end.center(), t);
    const Vec3 eye = lerp(start.eye(), end.eye(), t);
    Vec3 anchor = center;
    double centerShare = 0.0;
    const double centerMove = distance(start.center(), end.center());
    const double eyeMove = distance(start.eye(), end.eye());
    if (eyeMove <= kResolution) {
        anchor = eye;
        centerShare = 1.0;
    } else if (centerMove > kResolution) {
        centerShare = centerMove / (centerMove + eyeMove);
        const Vec3 anchorStart = lerp(start.center(), start.eye(), centerShare);
        const Vec3 anchorEnd = lerp(end.center(), end.eye(), centerShare);
        anchor = lerp(anchorStart, anchorEnd, t);
    }
    const Vec3 direction = camera.direction();
    const double reach = lerp(start.distance(), end.distance(), t);
    camera.setEyeAndCenter(anchor - direction * reach * (1.0 - centerShare), anchor + direction * reach * centerShare);

    if (std::abs(start.scale() - end.scale()) > kConfusion && start.isOrthographic()) {
        camera.setScale(lerp(start.scale(), end.scale(), t));
    }
    return camera;
}

Transition::Transition(const Camera& start, const Camera& end, double seconds)
    : m_start(start), m_end(end), m_seconds(std::isfinite(seconds) ? std::max(seconds, 0.0) : 0.0)
{}

Camera Transition::at(double elapsedSeconds) const
{
    if (m_seconds <= 0.0 || !std::isfinite(elapsedSeconds)) {
        return m_end;
    }
    return interpolate(m_start, m_end, std::clamp(elapsedSeconds / m_seconds, 0.0, 1.0));
}

} // namespace kalve137::camera
