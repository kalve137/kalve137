// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/mesh.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kalve137::pick {

using Point = snapshot::Point;

struct Hit {
    double distance = 0.0;
    std::uint32_t triangle = 0;
    std::uint32_t face = 0;
    std::uint32_t mesh = 0;
};

class RayScene {
public:
    explicit RayScene(const snapshot::Mesh& mesh);
    explicit RayScene(const std::vector<const snapshot::Mesh*>& meshes);
    ~RayScene();
    RayScene(const RayScene&) = delete;
    RayScene& operator=(const RayScene&) = delete;
    RayScene(RayScene&&) noexcept;
    RayScene& operator=(RayScene&&) noexcept;

    std::optional<Hit> first(const Point& origin, const Point& direction, double from = 0.0,
                             const std::vector<bool>* shown = nullptr) const;

private:
    struct Tree;
    std::unique_ptr<Tree> m_tree;
};

} // namespace kalve137::pick
