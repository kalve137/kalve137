// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kalve137::pick {

using Point = std::array<double, 3>;
using Triangle = std::array<std::uint32_t, 3>;

struct Mesh {
    std::vector<Point> vertices;
    std::vector<Triangle> triangles;
    std::vector<std::uint32_t> faces;
};

struct Hit {
    double distance = 0.0;
    std::uint32_t triangle = 0;
    std::uint32_t face = 0;
};

class RayScene {
public:
    explicit RayScene(const Mesh& mesh);
    ~RayScene();
    RayScene(const RayScene&) = delete;
    RayScene& operator=(const RayScene&) = delete;
    RayScene(RayScene&&) noexcept;
    RayScene& operator=(RayScene&&) noexcept;

    std::optional<Hit> first(const Point& origin, const Point& direction, double from = 0.0) const;

private:
    struct Tree;
    std::unique_ptr<Tree> m_tree;
};

} // namespace kalve137::pick
