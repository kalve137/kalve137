// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/math.h"

namespace kalve137::camera {

std::optional<Mat4> Mat4::inverted() const
{
    const std::array<double, 16>& a = m;
    std::array<double, 16> inv{};
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] -
              a[8] * a[2] * a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0.0 || !std::isfinite(det)) {
        return std::nullopt;
    }
    Mat4 result;
    for (std::size_t i = 0; i < inv.size(); ++i) {
        result.m[i] = inv[i] / det;
    }
    return result;
}

std::array<float, 16> Mat4::toFloat() const
{
    std::array<float, 16> result{};
    for (std::size_t i = 0; i < m.size(); ++i) {
        result[i] = static_cast<float>(m[i]);
    }
    return result;
}

Mat3 Mat3::transposed() const
{
    const auto& c = columns;
    return {{Vec3{c[0].x, c[1].x, c[2].x}, Vec3{c[0].y, c[1].y, c[2].y}, Vec3{c[0].z, c[1].z, c[2].z}}};
}

Motion Motion::rotationAbout(const Vec3& point, const Vec3& axis, double radians)
{
    const Vec3 u = normalized(axis);
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double t = 1.0 - c;
    Motion motion;
    motion.rotation.columns = {
        Vec3{t * u.x * u.x + c, t * u.x * u.y + s * u.z, t * u.x * u.z - s * u.y},
        Vec3{t * u.x * u.y - s * u.z, t * u.y * u.y + c, t * u.y * u.z + s * u.x},
        Vec3{t * u.x * u.z + s * u.y, t * u.y * u.z - s * u.x, t * u.z * u.z + c},
    };
    motion.translation = point - motion.rotation * point;
    return motion;
}

Motion Motion::translationBy(const Vec3& offset)
{
    Motion motion;
    motion.translation = offset;
    return motion;
}

Motion operator*(const Motion& a, const Motion& b)
{
    return {a.rotation * b.rotation, a.rotation * b.translation + a.translation};
}

std::array<Vec3, 8> Box::corners() const
{
    return {Vec3{min.x, min.y, min.z}, Vec3{min.x, min.y, max.z}, Vec3{min.x, max.y, min.z}, Vec3{min.x, max.y, max.z},
            Vec3{max.x, min.y, min.z}, Vec3{max.x, min.y, max.z}, Vec3{max.x, max.y, min.z}, Vec3{max.x, max.y, max.z}};
}

} // namespace kalve137::camera
