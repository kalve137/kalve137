// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kalve137::snapshot {

using Point = std::array<double, 3>;
using Triangle = std::array<std::uint32_t, 3>;
using Polyline = std::vector<Point>;

struct Edge {
    std::uint32_t id = 0;
    Polyline points;

    bool operator==(const Edge&) const = default;
};

struct Vertex {
    std::uint32_t id = 0;
    Point point{};

    bool operator==(const Vertex&) const = default;
};

struct Mesh {
    std::vector<Point> vertices;
    std::vector<Point> normals;
    std::vector<Triangle> triangles;
    std::vector<std::uint32_t> faces;
    std::uint32_t faceCount = 0;
    std::vector<double> faceDeflections;
    std::vector<Edge> edges;
    std::uint32_t edgeCount = 0;
    std::vector<Vertex> points;
    std::uint32_t pointCount = 0;
    Point low{};
    Point high{};
    bool closed = false;
};

inline void setBounds(Mesh& mesh)
{
    if (mesh.vertices.empty()) {
        mesh.low = {};
        mesh.high = {};
        return;
    }
    mesh.low = mesh.vertices.front();
    mesh.high = mesh.vertices.front();
    for (const Point& vertex : mesh.vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            mesh.low[axis] = std::min(mesh.low[axis], vertex[axis]);
            mesh.high[axis] = std::max(mesh.high[axis], vertex[axis]);
        }
    }
}

} // namespace kalve137::snapshot
