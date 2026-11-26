// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/snapshot_cache.h"

#include "geometry/body_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <vector>

namespace kalve137::core {

namespace {

constexpr double kOriginGrid = 1000.0;

snapshot::Point originOf(const std::vector<std::shared_ptr<const snapshot::Body>>& bodies)
{
    snapshot::Point low;
    snapshot::Point high;
    low.fill(std::numeric_limits<double>::infinity());
    high.fill(-std::numeric_limits<double>::infinity());
    for (const auto& body : bodies) {
        if (body->mesh->vertices.empty()) {
            continue;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], body->mesh->low[axis]);
            high[axis] = std::max(high[axis], body->mesh->high[axis]);
        }
    }
    snapshot::Point origin{};
    if (low[0] > high[0]) {
        return origin;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        origin[axis] = std::round((low[axis] + high[axis]) / 2.0 / kOriginGrid) * kOriginGrid;
    }
    return origin;
}

snapshot::Color snapshotColor(const geometry::Color& color)
{
    return {static_cast<float>(color.red), static_cast<float>(color.green), static_cast<float>(color.blue)};
}

snapshot::Appearance appearanceOf(const Body& body, std::uint32_t faceCount)
{
    snapshot::Appearance appearance;
    if (body.color) {
        appearance.color = snapshotColor(*body.color);
    }
    const std::vector<std::pair<int, geometry::Color>> entries = body.faceColors.toIndices(body.shape);
    if (!entries.empty()) {
        appearance.faces.resize(faceCount);
        for (const auto& [index, color] : entries) {
            const auto id = static_cast<std::size_t>(index - 1);
            if (id < appearance.faces.size()) {
                appearance.faces[id] = snapshotColor(color);
            }
        }
    }
    return appearance;
}

} // namespace

std::shared_ptr<const snapshot::Body> SnapshotCache::body(const Body& body)
{
    const BodyKey key = body.key();
    std::shared_ptr<const snapshot::Mesh> mesh;
    if (const auto found = m_entries.find(key);
        found != m_entries.end() && found->second.revision.IsEqual(body.shape)) {
        if (found->second.color == body.color && found->second.faceColors == body.faceColors) {
            return found->second.body;
        }
        mesh = found->second.body->mesh;
    }
    if (mesh == nullptr) {
        mesh = seedFor(body.shape);
    }
    if (mesh == nullptr) {
        mesh = std::make_shared<const snapshot::Mesh>(geometry::meshBody(body.shape));
    }
    auto built = std::make_shared<const snapshot::Body>(snapshot::Body{
        .feature = body.id, .part = body.part, .mesh = mesh, .appearance = appearanceOf(body, mesh->faceCount)});
    m_entries[key] = Entry{.revision = body.shape, .color = body.color, .faceColors = body.faceColors, .body = built};
    return built;
}

snapshot::Scene SnapshotCache::scene(const Model& model, const Visible& visible)
{
    std::vector<std::shared_ptr<const snapshot::Body>> all;
    snapshot::Scene scene;
    std::set<BodyKey> keys;
    for (const Body& body : model.bodies) {
        all.push_back(this->body(body));
        keys.insert(body.key());
        if (!visible || visible(body.key())) {
            scene.bodies.push_back(all.back());
        } else {
            scene.hidden.push_back(all.back());
        }
    }
    std::erase_if(m_entries, [&](const auto& entry) { return !keys.contains(entry.first); });
    m_seeds.clear();
    scene.origin = originOf(all);
    return scene;
}

void SnapshotCache::seed(const TopoDS_Shape& shape, std::shared_ptr<const snapshot::Mesh> mesh)
{
    m_seeds.push_back({.shape = shape, .mesh = std::move(mesh)});
}

void SnapshotCache::seedFrom(const SnapshotCache& other)
{
    for (const auto& [key, entry] : other.m_entries) {
        seed(entry.revision, entry.body->mesh);
    }
}

std::shared_ptr<const snapshot::Mesh> SnapshotCache::seedFor(const TopoDS_Shape& shape)
{
    const auto found = std::ranges::find_if(m_seeds, [&](const Seed& seed) { return seed.shape.IsEqual(shape); });
    return found != m_seeds.end() ? found->mesh : nullptr;
}

} // namespace kalve137::core
