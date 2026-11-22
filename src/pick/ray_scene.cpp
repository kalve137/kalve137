// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pick/ray_scene.h"

#include <tiny_bvh.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kalve137::pick {

struct RayScene::Tree {
    Point center{};
    std::vector<tinybvh::bvhvec4> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> faces;
    tinybvh::BVH bvh;
};

namespace {

Point centerOf(const std::vector<const snapshot::Mesh*>& meshes)
{
    bool any = false;
    Point low{};
    Point high{};
    for (const snapshot::Mesh* mesh : meshes) {
        for (const Point& vertex : mesh->vertices) {
            if (!any) {
                low = vertex;
                high = vertex;
                any = true;
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], vertex[axis]);
                high[axis] = std::max(high[axis], vertex[axis]);
            }
        }
    }
    return {(low[0] + high[0]) / 2.0, (low[1] + high[1]) / 2.0, (low[2] + high[2]) / 2.0};
}

} // namespace

RayScene::RayScene(const snapshot::Mesh& mesh) : RayScene(std::vector<const snapshot::Mesh*>{&mesh}) {}

RayScene::RayScene(const std::vector<const snapshot::Mesh*>& meshes) : m_tree(std::make_unique<Tree>())
{
    m_tree->center = centerOf(meshes);
    std::uint32_t faceBase = 0;
    for (const snapshot::Mesh* mesh : meshes) {
        const auto vertexBase = static_cast<std::uint32_t>(m_tree->vertices.size());
        for (const Point& vertex : mesh->vertices) {
            m_tree->vertices.emplace_back(static_cast<float>(vertex[0] - m_tree->center[0]),
                                          static_cast<float>(vertex[1] - m_tree->center[1]),
                                          static_cast<float>(vertex[2] - m_tree->center[2]), 0.0F);
        }
        for (const snapshot::Triangle& triangle : mesh->triangles) {
            for (const std::uint32_t corner : triangle) {
                m_tree->indices.push_back(vertexBase + corner);
            }
        }
        for (const std::uint32_t face : mesh->faces) {
            m_tree->faces.push_back(faceBase + face);
        }
        faceBase += mesh->faceCount;
    }
    if (!m_tree->faces.empty()) {
        m_tree->bvh.Build(m_tree->vertices.data(), m_tree->indices.data(),
                          static_cast<std::uint32_t>(m_tree->faces.size()));
    }
}

RayScene::~RayScene() = default;
RayScene::RayScene(RayScene&&) noexcept = default;
RayScene& RayScene::operator=(RayScene&&) noexcept = default;

std::optional<Hit> RayScene::first(const Point& origin, const Point& direction, double from) const
{
    if (m_tree->faces.empty()) {
        return std::nullopt;
    }
    const double length = std::hypot(direction[0], direction[1], direction[2]);
    if (length <= 0.0) {
        return std::nullopt;
    }
    const Point unit{direction[0] / length, direction[1] / length, direction[2] / length};
    const Point& center = m_tree->center;
    const tinybvh::bvhvec3 start(static_cast<float>(origin[0] - center[0] + unit[0] * from),
                                 static_cast<float>(origin[1] - center[1] + unit[1] * from),
                                 static_cast<float>(origin[2] - center[2] + unit[2] * from));
    tinybvh::Ray ray(
        start, tinybvh::bvhvec3(static_cast<float>(unit[0]), static_cast<float>(unit[1]), static_cast<float>(unit[2])));
    m_tree->bvh.Intersect(ray);
    if (!(ray.hit.t < BVH_FAR)) {
        return std::nullopt;
    }
    const std::uint32_t triangle = ray.hit.prim;
    return Hit{.distance = from + ray.hit.t, .triangle = triangle, .face = m_tree->faces[triangle]};
}

} // namespace kalve137::pick
