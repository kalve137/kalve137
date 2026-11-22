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

class SceneRays {
public:
    using Meshes = std::vector<std::shared_ptr<const snapshot::Mesh>>;

    void prepare(const snapshot::Scene& scene);
    std::shared_ptr<const RayScene> rays(const snapshot::Scene& scene);
    std::shared_ptr<const RayScene> rays(const Meshes& meshes);
    std::size_t builds() const { return m_builds; }

private:
    struct Build {
        Meshes meshes;
        std::future<std::shared_ptr<const RayScene>> rays;
    };

    bool running() const;

    Meshes m_meshes;
    std::shared_ptr<const RayScene> m_rays;
    std::optional<Build> m_pending;
    std::size_t m_builds = 0;
};

} // namespace kalve137::pick
