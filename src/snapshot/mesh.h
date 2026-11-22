// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
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
};

} // namespace kalve137::snapshot
