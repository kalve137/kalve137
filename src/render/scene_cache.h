// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kalve137::render {

class SceneCache {
public:
    DrawList draw(const snapshot::Scene& scene, const Frame& frame, const ViewportStyle& style);
    void clear();

private:
    struct MeshEntry {
        std::shared_ptr<const snapshot::Mesh> mesh;
        snapshot::Point origin{};
        std::vector<Position> positions;
        std::uint64_t positionsId = 0;
        std::vector<Normal> normals;
        std::uint64_t meshId = 0;
        std::uint64_t seen = 0;
    };
    struct ColorEntry {
        std::shared_ptr<const snapshot::Body> body;
        snapshot::Color fallback;
        std::optional<snapshot::Color> fill;
        std::vector<VertexColor> colors;
        std::uint64_t id = 0;
        std::uint64_t seen = 0;
    };

    MeshEntry& meshEntry(const std::shared_ptr<const snapshot::Mesh>& mesh, const snapshot::Point& origin);
    ColorEntry& colorEntry(const std::shared_ptr<const snapshot::Body>& body, const snapshot::Color& fallback,
                           const std::optional<snapshot::Color>& fill);
    void prune();

    std::unordered_map<const snapshot::Mesh*, MeshEntry> m_meshes;
    std::unordered_map<const snapshot::Body*, ColorEntry> m_colors;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_draw = 0;
};

} // namespace kalve137::render
