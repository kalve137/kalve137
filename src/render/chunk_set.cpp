// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/chunk_set.h"

#include "render/positions.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace kalve137::render {

namespace {

constexpr std::uint32_t kUnmapped = std::numeric_limits<std::uint32_t>::max();
constexpr unsigned kFaceShift = 32;

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

bool valid(const snapshot::Mesh& mesh, const snapshot::Triangle& triangle)
{
    return std::ranges::all_of(triangle, [&mesh](std::uint32_t index) { return index < mesh.vertices.size(); });
}

std::uint32_t facesOf(const snapshot::Mesh& mesh)
{
    return std::max<std::uint32_t>(mesh.faceCount, 1);
}

std::uint32_t piecesOf(const snapshot::Mesh& mesh)
{
    const std::uint32_t edges = std::max<std::uint32_t>(mesh.edgeCount, 1);
    return std::max((facesOf(mesh) - 1) / kPaletteEntries, (edges - 1) / kPaletteEntries) + 1;
}

std::uint32_t shareOf(std::uint32_t count, std::uint32_t piece)
{
    const std::uint32_t first = piece * kPaletteEntries;
    return first < count ? std::min(kPaletteEntries, count - first) : 0U;
}

std::uint32_t entriesOf(const snapshot::Mesh& mesh, std::uint32_t piece)
{
    return shareOf(facesOf(mesh), piece);
}

std::uint32_t edgeEntriesOf(const snapshot::Mesh& mesh, std::uint32_t piece)
{
    return shareOf(mesh.edgeCount, piece);
}

bool drawsAnything(const snapshot::Mesh& mesh)
{
    return !mesh.triangles.empty() || !mesh.edges.empty();
}

std::size_t verticesOf(const snapshot::Mesh& mesh)
{
    const std::size_t pieces = piecesOf(mesh);
    return (mesh.vertices.size() + pieces - 1) / pieces;
}

std::uint32_t faceAt(const snapshot::Mesh& mesh, std::size_t triangle)
{
    const std::uint32_t face = triangle < mesh.faces.size() ? mesh.faces[triangle] : 0U;
    return face < facesOf(mesh) ? face : 0U;
}

} // namespace

std::map<ChunkSet::Key, ChunkSet::Shown> ChunkSet::shownOf(const snapshot::Scene& scene) const
{
    std::map<Key, Shown> shown;
    const auto add = [&shown](const std::vector<std::shared_ptr<const snapshot::Body>>& bodies, bool visible) {
        for (const auto& body : bodies) {
            if (body == nullptr || body->mesh == nullptr || !drawsAnything(*body->mesh)) {
                continue;
            }
            for (std::uint32_t piece = 0; piece < piecesOf(*body->mesh); ++piece) {
                shown.try_emplace({body->feature, body->part, piece}, Shown{.body = body, .visible = visible});
            }
        }
    };
    add(scene.bodies, true);
    add(scene.hidden, false);
    return shown;
}

void ChunkSet::update(const snapshot::Scene& scene, const ViewportStyle& style)
{
    const bool lit = style.visualStyle != VisualStyle::VisibleEdges;
    const bool recolor = lit != m_lit || style.body != m_fallback || (!lit && style.background != m_fill);
    m_lit = lit;
    m_drawn = style.visualStyle != VisualStyle::Wireframe;
    m_edges = style.visualStyle != VisualStyle::Shaded;
    m_edgeColor = style.visualStyle == VisualStyle::ShadedWithEdges ? style.bodyEdge : style.wire;
    m_edgeWidth = style.visualStyle == VisualStyle::Wireframe ? kWireWidth : kBodyEdgeWidth;
    m_cullClosed = style.cullClosed;
    m_pixelRatio = style.pixelRatio;
    m_fallback = style.body;
    m_fill = style.background;
    if (scene.origin != m_origin) {
        m_origin = scene.origin;
        for (const auto& chunk : m_chunks) {
            chunk->meshDirty = true;
        }
    }

    const std::map<Key, Shown> shown = shownOf(scene);
    for (auto it = m_owner.begin(); it != m_owner.end();) {
        Data& chunk = *it->second;
        Slot& slot = chunk.members.at(it->first);
        const auto found = shown.find(it->first);
        bool keep = found != shown.end() && found->second.body->mesh->closed == chunk.closed;
        if (keep && found->second.body->mesh != slot.mesh) {
            const snapshot::Mesh& mesh = *found->second.body->mesh;
            const std::uint32_t piece = std::get<2>(it->first);
            const std::size_t vertices = chunk.vertexCount - slot.vertexCount + verticesOf(mesh);
            const std::size_t entries = chunk.entryCount - slot.entryCount + entriesOf(mesh, piece);
            const std::size_t edgeEntries = chunk.edgeEntryCount - slot.edgeEntryCount + edgeEntriesOf(mesh, piece);
            keep = (vertices <= m_vertexBudget || chunk.members.size() == 1) && entries <= kPaletteEntries &&
                   edgeEntries <= kPaletteEntries;
            if (keep) {
                chunk.vertexCount = vertices;
                chunk.entryCount = entries;
                chunk.edgeEntryCount = edgeEntries;
                slot.mesh = found->second.body->mesh;
                slot.vertexCount = verticesOf(mesh);
                slot.entryCount = entriesOf(mesh, piece);
                slot.edgeEntryCount = edgeEntriesOf(mesh, piece);
                chunk.meshDirty = true;
            }
        }
        if (!keep) {
            chunk.vertexCount -= slot.vertexCount;
            chunk.entryCount -= slot.entryCount;
            chunk.edgeEntryCount -= slot.edgeEntryCount;
            chunk.members.erase(it->first);
            chunk.meshDirty = true;
            it = m_owner.erase(it);
            continue;
        }
        if (found->second.body != slot.body || found->second.visible != slot.visible) {
            slot.body = found->second.body;
            slot.visible = found->second.visible;
            chunk.paletteDirty = true;
        }
        ++it;
    }
    for (const auto& [key, next] : shown) {
        if (!m_owner.contains(key)) {
            place(key, next);
        }
    }
    std::erase_if(m_chunks, [](const auto& chunk) { return chunk->members.empty(); });
    for (const auto& chunk : m_chunks) {
        if (chunk->meshDirty) {
            buildMesh(*chunk);
        } else if (chunk->paletteDirty || recolor) {
            buildPalette(*chunk);
        }
    }
}

void ChunkSet::place(const Key& key, const Shown& shown)
{
    const snapshot::Mesh& mesh = *shown.body->mesh;
    const std::uint32_t piece = std::get<2>(key);
    const std::size_t vertices = verticesOf(mesh);
    const std::uint32_t entries = entriesOf(mesh, piece);
    const std::uint32_t edgeEntries = edgeEntriesOf(mesh, piece);
    const auto fits = [&](const auto& chunk) {
        return chunk->closed == mesh.closed && chunk->vertexCount + vertices <= m_vertexBudget &&
               chunk->entryCount + entries <= kPaletteEntries && chunk->edgeEntryCount + edgeEntries <= kPaletteEntries;
    };
    auto found = std::ranges::find_if(m_chunks, fits);
    if (found == m_chunks.end()) {
        auto chunk = std::make_unique<Data>();
        chunk->id = m_nextId++;
        chunk->closed = mesh.closed;
        m_chunks.push_back(std::move(chunk));
        found = std::prev(m_chunks.end());
    }
    Data& chunk = **found;
    chunk.members.emplace(key, Slot{.body = shown.body,
                                    .mesh = shown.body->mesh,
                                    .visible = shown.visible,
                                    .firstFace = piece * kPaletteEntries,
                                    .firstEntry = 0,
                                    .entryCount = entries,
                                    .firstEdge = piece * kPaletteEntries,
                                    .firstEdgeEntry = 0,
                                    .edgeEntryCount = edgeEntries,
                                    .vertexCount = vertices});
    chunk.vertexCount += vertices;
    chunk.entryCount += entries;
    chunk.edgeEntryCount += edgeEntries;
    chunk.meshDirty = true;
    m_owner[key] = &chunk;
}

void ChunkSet::buildMesh(Data& chunk)
{
    chunk.vertices.clear();
    chunk.indices.clear();
    chunk.segments.clear();
    std::uint32_t entry = 0;
    std::uint32_t edgeEntry = 0;
    for (auto& [key, slot] : chunk.members) {
        slot.firstEntry = entry;
        entry += slot.entryCount;
        slot.firstEdgeEntry = edgeEntry;
        edgeEntry += slot.edgeEntryCount;
        addSegments(chunk, slot);
        const snapshot::Mesh& mesh = *slot.mesh;
        const std::vector<Normal> normals = normalsOf(mesh);
        const std::size_t first = chunk.vertices.size();
        std::vector<std::uint32_t> mapped(mesh.vertices.size(), kUnmapped);
        std::vector<std::uint32_t> faceOf(mesh.vertices.size(), 0);
        std::unordered_map<std::uint64_t, std::uint32_t> split;
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            const snapshot::Triangle& triangle = mesh.triangles[i];
            const std::uint32_t face = faceAt(mesh, i);
            if (face < slot.firstFace || face - slot.firstFace >= slot.entryCount || !valid(mesh, triangle)) {
                continue;
            }
            const auto add = [&](std::uint32_t index) {
                const auto at = static_cast<std::uint32_t>(chunk.vertices.size());
                chunk.vertices.push_back(packVertex(relativePosition(mesh.vertices[index], m_origin), normals[index],
                                                    slot.firstEntry + face - slot.firstFace));
                return at;
            };
            for (const std::uint32_t index : triangle) {
                if (mapped[index] == kUnmapped) {
                    mapped[index] = add(index);
                    faceOf[index] = face;
                    chunk.indices.push_back(mapped[index]);
                } else if (faceOf[index] == face) {
                    chunk.indices.push_back(mapped[index]);
                } else {
                    const std::uint64_t splitKey = (std::uint64_t{face} << kFaceShift) | index;
                    auto [it, added] = split.try_emplace(splitKey, 0);
                    if (added) {
                        it->second = add(index);
                    }
                    chunk.indices.push_back(it->second);
                }
            }
        }
        slot.vertexCount = chunk.vertices.size() - first;
    }
    chunk.vertexCount = chunk.vertices.size();
    chunk.entryCount = entry;
    chunk.edgeEntryCount = edgeEntry;
    chunk.meshRevision = m_nextId++;
    chunk.meshDirty = false;
    ++m_meshBuilds;
    buildPalette(chunk);
}

void ChunkSet::addSegments(Data& chunk, const Slot& slot) const
{
    for (const snapshot::Edge& edge : slot.mesh->edges) {
        if (edge.id < slot.firstEdge || edge.id - slot.firstEdge >= slot.edgeEntryCount) {
            continue;
        }
        const std::uint32_t entry = slot.firstEdgeEntry + edge.id - slot.firstEdge;
        for (std::size_t i = 1; i < edge.points.size(); ++i) {
            chunk.segments.push_back(packSegment(relativePosition(edge.points[i - 1], m_origin),
                                                 relativePosition(edge.points[i], m_origin), entry));
        }
    }
}

void ChunkSet::buildPalette(Data& chunk)
{
    chunk.palette.assign(chunk.entryCount, Entry{});
    chunk.edgePalette.assign(chunk.edgeEntryCount, Entry{});
    for (const auto& [key, slot] : chunk.members) {
        for (std::uint32_t face = 0; face < slot.entryCount; ++face) {
            chunk.palette[slot.firstEntry + face] = colorEntry(*slot.body, slot.visible, slot.firstFace + face);
        }
        const Entry edge{static_cast<std::uint8_t>(EdgeState::Normal), 0, 0,
                         slot.visible ? kEntryVisible : std::uint8_t{0}};
        std::fill_n(chunk.edgePalette.begin() + slot.firstEdgeEntry, slot.edgeEntryCount, edge);
    }
    chunk.paletteRevision = m_nextId++;
    chunk.paletteDirty = false;
    ++m_paletteBuilds;
}

Entry ChunkSet::colorEntry(const snapshot::Body& body, bool visible, std::uint32_t face) const
{
    snapshot::Color color = m_fill;
    if (m_lit) {
        const snapshot::Appearance& appearance = body.appearance;
        color = face < appearance.faces.size() && appearance.faces[face] ? *appearance.faces[face]
                                                                         : appearance.color.value_or(m_fallback);
    }
    const VertexColor encoded = vertexColor(color);
    return {encoded[0], encoded[1], encoded[2], visible ? kEntryVisible : std::uint8_t{0}};
}

std::optional<FaceEntry> ChunkSet::entryOf(int feature, int part, std::uint32_t face) const
{
    const auto found = m_owner.find({feature, part, face / kPaletteEntries});
    if (found == m_owner.end()) {
        return std::nullopt;
    }
    const Slot& slot = found->second->members.at(found->first);
    const std::uint32_t offset = face - slot.firstFace;
    if (offset >= slot.entryCount) {
        return std::nullopt;
    }
    return FaceEntry{.chunk = found->second->id, .entry = slot.firstEntry + offset};
}

DrawList ChunkSet::draw(const Frame& frame) const
{
    DrawList list{.frame = frame,
                  .lit = m_lit,
                  .cullClosed = m_cullClosed,
                  .pixelRatio = m_pixelRatio,
                  .chunks = {},
                  .faces = m_drawn,
                  .edges = m_edges,
                  .edgeColor = m_edgeColor,
                  .edgeWidth = m_edgeWidth};
    for (const auto& chunk : m_chunks) {
        if ((!m_drawn || chunk->indices.empty()) && (!m_edges || chunk->segments.empty())) {
            continue;
        }
        const bool shown = std::ranges::any_of(chunk->members, [](const auto& item) { return item.second.visible; });
        if (!shown) {
            continue;
        }
        list.chunks.push_back({.id = chunk->id,
                               .meshRevision = chunk->meshRevision,
                               .paletteRevision = chunk->paletteRevision,
                               .closed = chunk->closed,
                               .vertices = chunk->vertices,
                               .indices = chunk->indices,
                               .palette = chunk->palette,
                               .segments = chunk->segments,
                               .edgePalette = chunk->edgePalette});
    }
    return list;
}

void ChunkSet::clear()
{
    m_chunks.clear();
    m_owner.clear();
}

std::size_t ChunkSet::triangleCount() const
{
    std::size_t count = 0;
    for (const auto& chunk : m_chunks) {
        count += chunk->indices.size() / 3;
    }
    return count;
}

std::size_t ChunkSet::segmentCount() const
{
    std::size_t count = 0;
    for (const auto& chunk : m_chunks) {
        count += chunk->segments.size();
    }
    return count;
}

} // namespace kalve137::render
