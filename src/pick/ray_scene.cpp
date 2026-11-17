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

Point centerOf(const std::vector<Point>& vertices)
{
    if (vertices.empty()) {
        return {};
    }
    Point low = vertices.front();
    Point high = vertices.front();
    for (const Point& vertex : vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], vertex[axis]);
            high[axis] = std::max(high[axis], vertex[axis]);
        }
    }
    return {(low[0] + high[0]) / 2.0, (low[1] + high[1]) / 2.0, (low[2] + high[2]) / 2.0};
}

} // namespace

RayScene::RayScene(const Mesh& mesh) : m_tree(std::make_unique<Tree>())
{
    m_tree->center = centerOf(mesh.vertices);
    m_tree->vertices.reserve(mesh.vertices.size());
    for (const Point& vertex : mesh.vertices) {
        m_tree->vertices.emplace_back(static_cast<float>(vertex[0] - m_tree->center[0]),
                                      static_cast<float>(vertex[1] - m_tree->center[1]),
                                      static_cast<float>(vertex[2] - m_tree->center[2]), 0.0F);
    }
    m_tree->indices.reserve(mesh.triangles.size() * 3);
    for (const Triangle& triangle : mesh.triangles) {
        m_tree->indices.insert(m_tree->indices.end(), triangle.begin(), triangle.end());
    }
    m_tree->faces = mesh.faces;
    if (!mesh.triangles.empty()) {
        m_tree->bvh.Build(m_tree->vertices.data(), m_tree->indices.data(),
                          static_cast<std::uint32_t>(mesh.triangles.size()));
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
