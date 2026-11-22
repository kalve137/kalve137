// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pick/ray_scene.h"

#include <tiny_bvh.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace kalve137::pick {

constexpr double kHiddenStep = 1e-5;
constexpr double kHiddenReachStep = 1e-6;

struct RayScene::Tree {
    Point center{};
    double step = 0.0;
    std::vector<tinybvh::bvhvec4> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> faces;
    std::vector<std::uint32_t> firstTriangles;
    tinybvh::BVH bvh;
};

namespace {

struct Bounds {
    Point low{};
    Point high{};
};

Bounds boundsOf(const std::vector<const snapshot::Mesh*>& meshes)
{
    bool any = false;
    Bounds bounds;
    for (const snapshot::Mesh* mesh : meshes) {
        for (const Point& vertex : mesh->vertices) {
            if (!any) {
                bounds = {vertex, vertex};
                any = true;
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                bounds.low[axis] = std::min(bounds.low[axis], vertex[axis]);
                bounds.high[axis] = std::max(bounds.high[axis], vertex[axis]);
            }
        }
    }
    return bounds;
}

} // namespace

RayScene::RayScene(const snapshot::Mesh& mesh) : RayScene(std::vector<const snapshot::Mesh*>{&mesh}) {}

RayScene::RayScene(const std::vector<const snapshot::Mesh*>& meshes) : m_tree(std::make_unique<Tree>())
{
    const Bounds bounds = boundsOf(meshes);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        m_tree->center[axis] = (bounds.low[axis] + bounds.high[axis]) / 2.0;
    }
    m_tree->step = kHiddenStep * std::hypot(bounds.high[0] - bounds.low[0], bounds.high[1] - bounds.low[1],
                                            bounds.high[2] - bounds.low[2]);
    std::uint32_t faceBase = 0;
    for (const snapshot::Mesh* mesh : meshes) {
        m_tree->firstTriangles.push_back(static_cast<std::uint32_t>(m_tree->faces.size()));
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

std::optional<Hit> RayScene::first(const Point& origin, const Point& direction, double from,
                                   const std::vector<bool>* shown) const
{
    if (m_tree->faces.empty() || (shown != nullptr && std::ranges::none_of(*shown, std::identity{}))) {
        return std::nullopt;
    }
    const double length = std::hypot(direction[0], direction[1], direction[2]);
    if (length <= 0.0) {
        return std::nullopt;
    }
    const Point unit{direction[0] / length, direction[1] / length, direction[2] / length};
    const Point& center = m_tree->center;
    while (true) {
        const tinybvh::bvhvec3 start(static_cast<float>(origin[0] - center[0] + unit[0] * from),
                                     static_cast<float>(origin[1] - center[1] + unit[1] * from),
                                     static_cast<float>(origin[2] - center[2] + unit[2] * from));
        tinybvh::Ray ray(start, tinybvh::bvhvec3(static_cast<float>(unit[0]), static_cast<float>(unit[1]),
                                                 static_cast<float>(unit[2])));
        m_tree->bvh.Intersect(ray);
        if (!(ray.hit.t < BVH_FAR)) {
            return std::nullopt;
        }
        const std::uint32_t triangle = ray.hit.prim;
        const auto mesh = static_cast<std::uint32_t>(std::ranges::upper_bound(m_tree->firstTriangles, triangle) -
                                                     m_tree->firstTriangles.begin() - 1);
        const Hit hit{
            .distance = from + ray.hit.t, .triangle = triangle, .face = m_tree->faces[triangle], .mesh = mesh};
        if (shown == nullptr || (mesh < shown->size() && (*shown)[mesh])) {
            return hit;
        }
        const double reach = std::hypot(origin[0] - center[0], origin[1] - center[1], origin[2] - center[2]) + from;
        from = hit.distance + std::max(m_tree->step, kHiddenReachStep * reach);
    }
}

} // namespace kalve137::pick
