// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace kalve137::camera {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
    friend Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    friend Vec3 operator*(double s, const Vec3& a) { return a * s; }
    friend Vec3 operator/(const Vec3& a, double s) { return {a.x / s, a.y / s, a.z / s}; }
    bool operator==(const Vec3&) const = default;
};

inline double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double length(const Vec3& a)
{
    return std::sqrt(dot(a, a));
}

inline double distance(const Vec3& a, const Vec3& b)
{
    return length(a - b);
}

inline bool isFinite(const Vec3& a)
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

inline Vec3 normalized(const Vec3& a)
{
    const double size = length(a);
    return size > 0.0 ? a / size : a;
}

inline double angle(const Vec3& a, const Vec3& b)
{
    return std::atan2(length(cross(a, b)), dot(a, b));
}

struct Vec4 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

struct Mat4 {
    std::array<double, 16> m{1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};

    static std::size_t index(int row, int column)
    {
        return (static_cast<std::size_t>(column) * 4) + static_cast<std::size_t>(row);
    }
    double& at(int row, int column) { return m[index(row, column)]; }
    double at(int row, int column) const { return m[index(row, column)]; }

    friend Mat4 operator*(const Mat4& a, const Mat4& b)
    {
        Mat4 product;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) {
                    sum += a.at(row, k) * b.at(k, column);
                }
                product.at(row, column) = sum;
            }
        }
        return product;
    }

    friend Vec4 operator*(const Mat4& a, const Vec4& v)
    {
        const auto row = [&](int r) {
            return a.at(r, 0) * v.x + a.at(r, 1) * v.y + a.at(r, 2) * v.z + a.at(r, 3) * v.w;
        };
        return {row(0), row(1), row(2), row(3)};
    }

    std::optional<Mat4> inverted() const;
    std::array<float, 16> toFloat() const;
};

struct Mat3 {
    std::array<Vec3, 3> columns{Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 1.0}};

    friend Vec3 operator*(const Mat3& a, const Vec3& v)
    {
        return a.columns[0] * v.x + a.columns[1] * v.y + a.columns[2] * v.z;
    }
    friend Mat3 operator*(const Mat3& a, const Mat3& b)
    {
        return {{a * b.columns[0], a * b.columns[1], a * b.columns[2]}};
    }
    Mat3 transposed() const;
};

struct Motion {
    Mat3 rotation;
    Vec3 translation;

    static Motion rotationAbout(const Vec3& point, const Vec3& axis, double radians);
    static Motion translationBy(const Vec3& offset);
    friend Motion operator*(const Motion& a, const Motion& b);
    Vec3 point(const Vec3& p) const { return rotation * p + translation; }
    Vec3 direction(const Vec3& d) const { return rotation * d; }
};

struct Box {
    Vec3 min;
    Vec3 max;

    Vec3 center() const { return (min + max) * 0.5; }
    std::array<Vec3, 8> corners() const;
};

} // namespace kalve137::camera
