// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/draw_list.h"
#include "render/pick_id.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace kalve137::render {

struct FaceEntry {
    std::uint64_t chunk = 0;
    std::uint32_t entry = 0;

    bool operator==(const FaceEntry&) const = default;
};

struct PickedItem {
    int feature = 0;
    int part = 0;
    PickKind kind = PickKind::None;
    std::uint32_t index = 0;

    bool operator==(const PickedItem&) const = default;
};

class ChunkSet {
public:
    static constexpr std::size_t kDefaultVertexBudget = std::size_t{1} << 16;

    explicit ChunkSet(std::size_t vertexBudget = kDefaultVertexBudget) : m_vertexBudget(vertexBudget) {}

    void update(const snapshot::Scene& scene, const ViewportStyle& style);
    DrawList draw(const Frame& frame) const;
    DrawList pickList(const Frame& frame) const;
    void clear();
    std::optional<FaceEntry> entryOf(int feature, int part, std::uint32_t face) const;
    std::optional<PickedItem> itemOf(std::uint64_t chunk, PickKind kind, std::uint32_t entry) const;

    std::size_t chunkCount() const { return m_chunks.size(); }
    std::size_t triangleCount() const;
    std::size_t segmentCount() const;
    std::uint64_t meshBuilds() const { return m_meshBuilds; }
    std::uint64_t paletteBuilds() const { return m_paletteBuilds; }

private:
    using Key = std::tuple<int, int, std::uint32_t>;

    struct Slot {
        std::shared_ptr<const snapshot::Body> body;
        std::shared_ptr<const snapshot::Mesh> mesh;
        bool visible = true;
        std::uint32_t firstFace = 0;
        std::uint32_t firstEntry = 0;
        std::uint32_t entryCount = 0;
        std::uint32_t firstEdge = 0;
        std::uint32_t firstEdgeEntry = 0;
        std::uint32_t edgeEntryCount = 0;
        std::uint32_t firstPoint = 0;
        std::uint32_t firstPointEntry = 0;
        std::uint32_t pointEntryCount = 0;
        std::size_t vertexCount = 0;
    };
    struct Data {
        std::uint64_t id = 0;
        std::uint64_t meshRevision = 0;
        std::uint64_t paletteRevision = 0;
        bool closed = false;
        std::map<Key, Slot> members;
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<Entry> palette;
        std::vector<Segment> segments;
        std::vector<Entry> edgePalette;
        std::vector<Segment> points;
        std::vector<Entry> pointPalette;
        std::size_t vertexCount = 0;
        std::size_t entryCount = 0;
        std::size_t edgeEntryCount = 0;
        std::size_t pointEntryCount = 0;
        bool meshDirty = false;
        bool paletteDirty = false;
    };
    struct Shown {
        std::shared_ptr<const snapshot::Body> body;
        bool visible = true;
    };

    std::map<Key, Shown> shownOf(const snapshot::Scene& scene) const;
    void place(const Key& key, const Shown& shown);
    void buildMesh(Data& chunk);
    void addSegments(Data& chunk, const Slot& slot) const;
    void addPoints(Data& chunk, const Slot& slot) const;
    Chunk chunkOf(const Data& chunk) const;
    void buildPalette(Data& chunk);
    Entry colorEntry(const snapshot::Body& body, bool visible, std::uint32_t face) const;

    std::size_t m_vertexBudget;
    std::vector<std::unique_ptr<Data>> m_chunks;
    std::map<Key, Data*> m_owner;
    snapshot::Point m_origin{};
    bool m_lit = true;
    bool m_drawn = true;
    bool m_edges = false;
    bool m_cullClosed = true;
    double m_pixelRatio = 1.0;
    double m_edgeWidth = 0.0;
    snapshot::Color m_edgeColor;
    snapshot::Color m_fallback;
    snapshot::Color m_fill;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_meshBuilds = 0;
    std::uint64_t m_paletteBuilds = 0;
};

} // namespace kalve137::render
