// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/snapshot_cache.h"

#include "geometry/body_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>

namespace kalve137::core {

namespace {

constexpr double kOriginGrid = 1000.0;

snapshot::Point originOf(const snapshot::Scene& scene)
{
    snapshot::Point low;
    snapshot::Point high;
    low.fill(std::numeric_limits<double>::infinity());
    high.fill(-std::numeric_limits<double>::infinity());
    for (const auto& body : scene.bodies) {
        for (const snapshot::Point& vertex : body->mesh.vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], vertex[axis]);
                high[axis] = std::max(high[axis], vertex[axis]);
            }
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

} // namespace

std::shared_ptr<const snapshot::Body> SnapshotCache::body(const Body& body)
{
    const BodyKey key = body.key();
    if (const auto found = m_entries.find(key);
        found != m_entries.end() && found->second.revision.IsEqual(body.shape)) {
        return found->second.body;
    }
    auto built = std::make_shared<const snapshot::Body>(
        snapshot::Body{.feature = body.id, .part = body.part, .mesh = geometry::meshBody(body.shape)});
    m_entries[key] = Entry{.revision = body.shape, .body = built};
    return built;
}

snapshot::Scene SnapshotCache::scene(const Model& model)
{
    snapshot::Scene scene;
    std::set<BodyKey> keys;
    for (const Body& body : model.bodies) {
        scene.bodies.push_back(this->body(body));
        keys.insert(body.key());
    }
    std::erase_if(m_entries, [&](const auto& entry) { return !keys.contains(entry.first); });
    scene.origin = originOf(scene);
    return scene;
}

} // namespace kalve137::core
