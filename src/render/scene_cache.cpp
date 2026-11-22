// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/scene_cache.h"

#include <cstddef>

namespace kalve137::render {

namespace {

Normal toNormal(const snapshot::Point& point)
{
    return {static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2])};
}

std::vector<Normal> normalsOf(const snapshot::Mesh& mesh)
{
    std::vector<Normal> normals;
    normals.reserve(mesh.vertices.size());
    if (mesh.normals.size() == mesh.vertices.size()) {
        for (const snapshot::Point& normal : mesh.normals) {
            normals.push_back(toNormal(normal));
        }
        return normals;
    }
    normals.assign(mesh.vertices.size(), Normal{0.0F, 0.0F, 1.0F});
    for (const snapshot::Triangle& triangle : mesh.triangles) {
        const snapshot::Point& a = mesh.vertices[triangle[0]];
        const snapshot::Point& b = mesh.vertices[triangle[1]];
        const snapshot::Point& c = mesh.vertices[triangle[2]];
        const camera::Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const camera::Vec3 v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const camera::Vec3 n = camera::normalized(camera::cross(u, v));
        for (const std::uint32_t index : triangle) {
            normals[index] = toNormal({n.x, n.y, n.z});
        }
    }
    return normals;
}

std::vector<VertexColor> colorsOf(const snapshot::Body& body, const snapshot::Color& fallback,
                                  const std::optional<snapshot::Color>& fill)
{
    const snapshot::Mesh& mesh = *body.mesh;
    if (fill) {
        return std::vector<VertexColor>(mesh.vertices.size(), vertexColor(*fill));
    }
    const snapshot::Appearance& appearance = body.appearance;
    const VertexColor base = vertexColor(appearance.color.value_or(fallback));
    std::vector<VertexColor> colors(mesh.vertices.size(), base);
    if (appearance.faces.empty()) {
        return colors;
    }
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
        const std::uint32_t face = mesh.faces[i];
        if (face >= appearance.faces.size() || !appearance.faces[face]) {
            continue;
        }
        const VertexColor color = vertexColor(*appearance.faces[face]);
        for (const std::uint32_t index : mesh.triangles[i]) {
            colors[index] = color;
        }
    }
    return colors;
}

} // namespace

DrawList SceneCache::draw(const snapshot::Scene& scene, const Frame& frame, const ViewportStyle& style)
{
    DrawList list{.frame = frame, .batches = {}};
    if (style.visualStyle == VisualStyle::Wireframe) {
        return list;
    }
    ++m_draw;
    const bool lit = style.visualStyle != VisualStyle::VisibleEdges;
    const std::optional<snapshot::Color> fill = lit ? std::nullopt : std::optional<snapshot::Color>(style.background);
    for (const auto& body : scene.bodies) {
        if (body == nullptr || body->mesh == nullptr || body->mesh->triangles.empty()) {
            continue;
        }
        const MeshEntry& mesh = meshEntry(body->mesh, scene.origin);
        const ColorEntry& colors = colorEntry(body, style.body, fill);
        list.batches.push_back({
            .kind = ItemKind::Faces,
            .lit = lit,
            .positions = {mesh.positionsId, mesh.positions},
            .normals = {mesh.meshId, mesh.normals},
            .triangles = {mesh.meshId, mesh.mesh->triangles},
            .colors = {colors.id, colors.colors},
        });
    }
    prune();
    return list;
}

void SceneCache::clear()
{
    m_meshes.clear();
    m_colors.clear();
}

SceneCache::MeshEntry& SceneCache::meshEntry(const std::shared_ptr<const snapshot::Mesh>& mesh,
                                             const snapshot::Point& origin)
{
    auto [it, added] = m_meshes.try_emplace(mesh.get());
    MeshEntry& entry = it->second;
    if (added) {
        entry.mesh = mesh;
        entry.normals = normalsOf(*mesh);
        entry.meshId = m_nextId++;
    }
    if (added || entry.origin != origin) {
        entry.origin = origin;
        entry.positions = relativePositions(*mesh, origin);
        entry.positionsId = m_nextId++;
    }
    entry.seen = m_draw;
    return entry;
}

SceneCache::ColorEntry& SceneCache::colorEntry(const std::shared_ptr<const snapshot::Body>& body,
                                               const snapshot::Color& fallback,
                                               const std::optional<snapshot::Color>& fill)
{
    auto [it, added] = m_colors.try_emplace(body.get());
    ColorEntry& entry = it->second;
    if (added || entry.fallback != fallback || entry.fill != fill) {
        entry.body = body;
        entry.fallback = fallback;
        entry.fill = fill;
        entry.colors = colorsOf(*body, fallback, fill);
        entry.id = m_nextId++;
    }
    entry.seen = m_draw;
    return entry;
}

void SceneCache::prune()
{
    std::erase_if(m_meshes, [this](const auto& item) { return item.second.seen != m_draw; });
    std::erase_if(m_colors, [this](const auto& item) { return item.second.seen != m_draw; });
}

} // namespace kalve137::render
