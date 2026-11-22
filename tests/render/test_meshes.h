// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/body.h"
#include "snapshot/mesh.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <utility>

namespace kalve137::render::test {

inline constexpr double kTorusMajor = 40.0;
inline constexpr double kTorusMinor = 12.0;

inline std::shared_ptr<const snapshot::Mesh> torus(const snapshot::Point& center, int rings = 96, int segments = 48)
{
    snapshot::Mesh mesh;
    for (int i = 0; i < rings; ++i) {
        const double a = 2.0 * std::numbers::pi * i / rings;
        for (int j = 0; j < segments; ++j) {
            const double b = 2.0 * std::numbers::pi * j / segments;
            const snapshot::Point normal{std::cos(b) * std::cos(a), std::cos(b) * std::sin(a), std::sin(b)};
            mesh.vertices.push_back({center[0] + (kTorusMajor * std::cos(a)) + (kTorusMinor * normal[0]),
                                     center[1] + (kTorusMajor * std::sin(a)) + (kTorusMinor * normal[1]),
                                     center[2] + (kTorusMinor * normal[2])});
            mesh.normals.push_back(normal);
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const auto p = static_cast<std::uint32_t>((i * segments) + j);
            const auto q = static_cast<std::uint32_t>((((i + 1) % rings) * segments) + j);
            const auto r = static_cast<std::uint32_t>((((i + 1) % rings) * segments) + ((j + 1) % segments));
            const auto s = static_cast<std::uint32_t>((i * segments) + ((j + 1) % segments));
            mesh.triangles.push_back({p, q, r});
            mesh.triangles.push_back({p, r, s});
            mesh.faces.insert(mesh.faces.end(), {0, 0});
        }
    }
    mesh.faceCount = 1;
    mesh.closed = true;
    snapshot::setBounds(mesh);
    return std::make_shared<const snapshot::Mesh>(std::move(mesh));
}

inline std::shared_ptr<const snapshot::Mesh> box(const snapshot::Point& low, const snapshot::Point& high,
                                                 bool closed = true)
{
    snapshot::Mesh mesh;
    for (std::uint32_t face = 0; face < 6; ++face) {
        const std::size_t axis = face / 2;
        const bool upper = face % 2 == 1;
        const std::size_t u = (axis + 1) % 3;
        const std::size_t v = (axis + 2) % 3;
        snapshot::Point normal{};
        normal[axis] = upper ? 1.0 : -1.0;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& [du, dv] :
             std::array<std::array<bool, 2>, 4>{{{false, false}, {true, false}, {true, true}, {false, true}}}) {
            snapshot::Point point{};
            point[axis] = upper ? high[axis] : low[axis];
            point[u] = du ? high[u] : low[u];
            point[v] = dv ? high[v] : low[v];
            mesh.vertices.push_back(point);
            mesh.normals.push_back(normal);
        }
        if (upper) {
            mesh.triangles.push_back({base, base + 1, base + 2});
            mesh.triangles.push_back({base, base + 2, base + 3});
        } else {
            mesh.triangles.push_back({base, base + 2, base + 1});
            mesh.triangles.push_back({base, base + 3, base + 2});
        }
        mesh.faces.insert(mesh.faces.end(), {face, face});
    }
    mesh.faceCount = 6;
    mesh.closed = closed;
    snapshot::setBounds(mesh);
    return std::make_shared<const snapshot::Mesh>(std::move(mesh));
}

inline std::shared_ptr<const snapshot::Mesh> edgedBox(const snapshot::Point& low, const snapshot::Point& high,
                                                      bool closed = true)
{
    snapshot::Mesh mesh = *box(low, high, closed);
    const auto corner = [&](int bits) {
        return snapshot::Point{(bits & 1) != 0 ? high[0] : low[0], (bits & 2) != 0 ? high[1] : low[1],
                               (bits & 4) != 0 ? high[2] : low[2]};
    };
    for (int from = 0; from < 8; ++from) {
        for (const int axis : {1, 2, 4}) {
            if ((from & axis) == 0) {
                mesh.edges.push_back({.id = mesh.edgeCount++, .points = {corner(from), corner(from | axis)}});
            }
        }
        mesh.points.push_back({.id = mesh.pointCount++, .point = corner(from)});
    }
    return std::make_shared<const snapshot::Mesh>(std::move(mesh));
}

inline std::shared_ptr<const snapshot::Mesh> grid(const snapshot::Point& center, double size, int cells)
{
    const auto point = [&](int i, int j) {
        const double step = size / cells;
        const bool inner = i > 0 && j > 0 && i < cells && j < cells;
        const double jitter = inner ? 0.3 * step : 0.0;
        return snapshot::Point{center[0] - (size / 2) + (i * step) + (jitter * std::sin((i * 7.1) + (j * 3.3))),
                               center[1] - (size / 2) + (j * step) + (jitter * std::cos((i * 2.7) - (j * 5.9))),
                               center[2]};
    };
    snapshot::Mesh mesh;
    std::uint32_t face = 0;
    for (int i = 0; i < cells; ++i) {
        for (int j = 0; j < cells; ++j, ++face) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const snapshot::Point& corner : {point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1)}) {
                mesh.vertices.push_back(corner);
                mesh.normals.push_back({0.0, 0.0, 1.0});
            }
            if ((i + j) % 2 == 0) {
                mesh.triangles.push_back({base, base + 1, base + 2});
                mesh.triangles.push_back({base, base + 2, base + 3});
            } else {
                mesh.triangles.push_back({base, base + 1, base + 3});
                mesh.triangles.push_back({base + 1, base + 2, base + 3});
            }
            mesh.faces.insert(mesh.faces.end(), {face, face});
        }
    }
    mesh.faceCount = face;
    snapshot::setBounds(mesh);
    return std::make_shared<const snapshot::Mesh>(std::move(mesh));
}

inline snapshot::Body bodyOf(const std::shared_ptr<const snapshot::Mesh>& mesh, snapshot::Appearance appearance = {},
                             int part = 0)
{
    return {.feature = 1, .part = part, .mesh = mesh, .appearance = std::move(appearance)};
}

inline std::shared_ptr<const snapshot::Body> shared(snapshot::Body body)
{
    return std::make_shared<const snapshot::Body>(std::move(body));
}

} // namespace kalve137::render::test
