// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pick/ray_scene.h"
#include "snapshot/body.h"
#include "snapshot/mesh.h"

#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace kalve137::pick {

using Meshes = std::vector<std::shared_ptr<const snapshot::Mesh>>;

struct VisibleRays {
    std::shared_ptr<const RayScene> scene;
    Meshes meshes;
    std::vector<bool> shown;

    std::optional<Hit> first(const Point& origin, const Point& direction, double from = 0.0) const;
};

class SceneRays {
public:
    using Meshes = pick::Meshes;

    void prepare(const snapshot::Scene& scene);
    VisibleRays rays(const snapshot::Scene& scene);
    VisibleRays rays(const Meshes& meshes, const Meshes& visible);
    std::size_t builds() const { return m_builds; }

private:
    struct Build {
        Meshes meshes;
        std::future<std::shared_ptr<const RayScene>> rays;
    };

    bool running() const;
    std::shared_ptr<const RayScene> sceneOf(const Meshes& meshes);

    Meshes m_meshes;
    std::shared_ptr<const RayScene> m_rays;
    std::optional<Build> m_pending;
    std::size_t m_builds = 0;
};

} // namespace kalve137::pick
