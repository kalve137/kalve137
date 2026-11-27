// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pick/scene_rays.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace kalve137::pick {

namespace {

Meshes ordered(Meshes meshes)
{
    std::erase(meshes, nullptr);
    std::ranges::sort(meshes, std::less{}, &std::shared_ptr<const snapshot::Mesh>::get);
    const auto [first, last] = std::ranges::unique(meshes);
    meshes.erase(first, last);
    return meshes;
}

Meshes meshesOf(const std::vector<std::shared_ptr<const snapshot::Body>>& bodies)
{
    Meshes meshes;
    meshes.reserve(bodies.size());
    for (const auto& body : bodies) {
        meshes.push_back(body->mesh);
    }
    return meshes;
}

Meshes allMeshesOf(const snapshot::Scene& scene)
{
    Meshes meshes = meshesOf(scene.bodies);
    const Meshes hidden = meshesOf(scene.hidden);
    meshes.insert(meshes.end(), hidden.begin(), hidden.end());
    return ordered(std::move(meshes));
}

std::shared_ptr<const RayScene> build(const Meshes& meshes)
{
    std::vector<const snapshot::Mesh*> pointers;
    pointers.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        pointers.push_back(mesh.get());
    }
    return std::make_shared<const RayScene>(pointers);
}

} // namespace

std::optional<Hit> VisibleRays::first(const Point& origin, const Point& direction, double from) const
{
    if (scene == nullptr) {
        return std::nullopt;
    }
    return scene->first(origin, direction, from, &shown);
}

bool SceneRays::running() const
{
    return m_pending && m_pending->rays.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void SceneRays::prepare(const snapshot::Scene& scene)
{
    Meshes meshes = allMeshesOf(scene);
    if (meshes.empty() || (m_rays != nullptr && meshes == m_meshes) || (m_pending && meshes == m_pending->meshes) ||
        running()) {
        return;
    }
    auto rays = std::async(std::launch::async, [meshes] { return build(meshes); });
    m_pending = Build{std::move(meshes), std::move(rays)};
    ++m_builds;
}

VisibleRays SceneRays::rays(const snapshot::Scene& scene)
{
    return rays(allMeshesOf(scene), meshesOf(scene.bodies));
}

VisibleRays SceneRays::rays(const Meshes& meshes, const Meshes& visible)
{
    VisibleRays result{.scene = nullptr, .meshes = ordered(meshes), .shown = {}};
    result.scene = sceneOf(result.meshes);
    const Meshes shown = ordered(visible);
    result.shown.reserve(result.meshes.size());
    for (const auto& mesh : result.meshes) {
        result.shown.push_back(
            std::ranges::binary_search(shown, mesh.get(), std::less{}, &std::shared_ptr<const snapshot::Mesh>::get));
    }
    return result;
}

std::shared_ptr<const RayScene> SceneRays::sceneOf(const Meshes& meshes)
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
