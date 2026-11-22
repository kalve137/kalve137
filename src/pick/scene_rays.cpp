// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pick/scene_rays.h"

#include <chrono>
#include <utility>

namespace kalve137::pick {

namespace {

SceneRays::Meshes meshesOf(const snapshot::Scene& scene)
{
    SceneRays::Meshes meshes;
    meshes.reserve(scene.bodies.size());
    for (const auto& body : scene.bodies) {
        meshes.push_back(body->mesh);
    }
    return meshes;
}

std::shared_ptr<const RayScene> build(const SceneRays::Meshes& meshes)
{
    std::vector<const snapshot::Mesh*> pointers;
    pointers.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        pointers.push_back(mesh.get());
    }
    return std::make_shared<const RayScene>(pointers);
}

} // namespace

bool SceneRays::running() const
{
    return m_pending && m_pending->rays.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void SceneRays::prepare(const snapshot::Scene& scene)
{
    Meshes meshes = meshesOf(scene);
    if (meshes.empty() || (m_rays != nullptr && meshes == m_meshes) || (m_pending && meshes == m_pending->meshes) ||
        running()) {
        return;
    }
    auto rays = std::async(std::launch::async, [meshes] { return build(meshes); });
    m_pending = Build{std::move(meshes), std::move(rays)};
    ++m_builds;
}

std::shared_ptr<const RayScene> SceneRays::rays(const snapshot::Scene& scene)
{
    return rays(meshesOf(scene));
}

std::shared_ptr<const RayScene> SceneRays::rays(const Meshes& meshes)
{
    if (meshes.empty()) {
        return nullptr;
    }
    if (m_pending && meshes == m_pending->meshes) {
        Build pending = std::move(*m_pending);
        m_pending.reset();
        m_rays = pending.rays.get();
        m_meshes = std::move(pending.meshes);
        return m_rays;
    }
    if (m_rays != nullptr && meshes == m_meshes) {
        return m_rays;
    }
    m_rays = build(meshes);
    m_meshes = meshes;
    ++m_builds;
    return m_rays;
}

} // namespace kalve137::pick
