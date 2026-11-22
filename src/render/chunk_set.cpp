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
    const std::uint32_t points = std::max<std::uint32_t>(mesh.pointCount, 1);
    return std::max(
               {(facesOf(mesh) - 1) / kPaletteEntries, (edges - 1) / kPaletteEntries, (points - 1) / kPaletteEntries}) +
           1;
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

std::uint32_t pointEntriesOf(const snapshot::Mesh& mesh, std::uint32_t piece)
{
    return shareOf(mesh.pointCount, piece);
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

constexpr std::uint32_t kNoEdge = std::numeric_limits<std::uint32_t>::max();

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
            const std::size_t pointEntries = chunk.pointEntryCount - slot.pointEntryCount + pointEntriesOf(mesh, piece);
            keep = (vertices <= m_vertexBudget || chunk.members.size() == 1) && entries <= kPaletteEntries &&
                   edgeEntries <= kPaletteEntries && pointEntries <= kPaletteEntries;
            if (keep) {
                chunk.vertexCount = vertices;
                chunk.entryCount = entries;
                chunk.edgeEntryCount = edgeEntries;
                chunk.pointEntryCount = pointEntries;
                slot.mesh = found->second.body->mesh;
                slot.vertexCount = verticesOf(mesh);
                slot.entryCount = entriesOf(mesh, piece);
                slot.edgeEntryCount = edgeEntriesOf(mesh, piece);
                slot.pointEntryCount = pointEntriesOf(mesh, piece);
                chunk.meshDirty = true;
            }
        }
        if (!keep) {
            chunk.vertexCount -= slot.vertexCount;
            chunk.entryCount -= slot.entryCount;
            chunk.edgeEntryCount -= slot.edgeEntryCount;
            chunk.pointEntryCount -= slot.pointEntryCount;
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
    buildHighlightGeometry(true);
}

void ChunkSet::place(const Key& key, const Shown& shown)
{
    const snapshot::Mesh& mesh = *shown.body->mesh;
    const std::uint32_t piece = std::get<2>(key);
    const std::size_t vertices = verticesOf(mesh);
    const std::uint32_t entries = entriesOf(mesh, piece);
    const std::uint32_t edgeEntries = edgeEntriesOf(mesh, piece);
    const std::uint32_t pointEntries = pointEntriesOf(mesh, piece);
    const auto fits = [&](const auto& chunk) {
        return chunk->closed == mesh.closed && chunk->vertexCount + vertices <= m_vertexBudget &&
               chunk->entryCount + entries <= kPaletteEntries &&
               chunk->edgeEntryCount + edgeEntries <= kPaletteEntries &&
               chunk->pointEntryCount + pointEntries <= kPaletteEntries;
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
                                    .firstPoint = piece * kPaletteEntries,
                                    .firstPointEntry = 0,
                                    .pointEntryCount = pointEntries,
                                    .vertexCount = vertices});
    chunk.vertexCount += vertices;
    chunk.entryCount += entries;
    chunk.edgeEntryCount += edgeEntries;
    chunk.pointEntryCount += pointEntries;
    chunk.meshDirty = true;
    m_owner[key] = &chunk;
}

void ChunkSet::buildMesh(Data& chunk)
{
    chunk.vertices.clear();
    chunk.indices.clear();
    chunk.segments.clear();
    chunk.points.clear();
    std::uint32_t entry = 0;
    std::uint32_t edgeEntry = 0;
    std::uint32_t pointEntry = 0;
    for (auto& [key, slot] : chunk.members) {
        slot.firstEntry = entry;
        entry += slot.entryCount;
        slot.firstEdgeEntry = edgeEntry;
        edgeEntry += slot.edgeEntryCount;
        slot.firstPointEntry = pointEntry;
        pointEntry += slot.pointEntryCount;
        addSegments(chunk, slot);
        addPoints(chunk, slot);
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
    chunk.pointEntryCount = pointEntry;
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

void ChunkSet::addPoints(Data& chunk, const Slot& slot) const
{
    for (const snapshot::Vertex& point : slot.mesh->points) {
        if (point.id < slot.firstPoint || point.id - slot.firstPoint >= slot.pointEntryCount) {
            continue;
        }
        const Position at = relativePosition(point.point, m_origin);
        chunk.points.push_back(packSegment(at, at, slot.firstPointEntry + point.id - slot.firstPoint));
    }
}

void ChunkSet::buildPalette(Data& chunk)
{
    chunk.palette.assign(chunk.entryCount, Entry{});
    chunk.edgePalette.assign(chunk.edgeEntryCount, Entry{});
    chunk.pointPalette.assign(chunk.pointEntryCount, Entry{});
    for (const auto& [key, slot] : chunk.members) {
        for (std::uint32_t face = 0; face < slot.entryCount; ++face) {
            chunk.palette[slot.firstEntry + face] = colorEntry(*slot.body, slot.visible, slot.firstFace + face);
        }
        const Entry edge{static_cast<std::uint8_t>(EdgeState::Normal), 0, 0,
                         slot.visible ? kEntryVisible : std::uint8_t{0}};
        std::fill_n(chunk.edgePalette.begin() + slot.firstEdgeEntry, slot.edgeEntryCount, edge);
        std::fill_n(chunk.pointPalette.begin() + slot.firstPointEntry, slot.pointEntryCount, edge);
    }
    for (const std::set<PickedItem>* items :
         {&m_highlight.hovered, &m_highlight.selected, &m_highlight.hoveredOutline, &m_highlight.selectedOutline}) {
        for (const PickedItem& item : *items) {
            Data* owner = nullptr;
            if (Entry* entry = entryFor(item, &owner); entry != nullptr && owner == &chunk) {
                writeState(item, *entry);
            }
        }
    }
    chunk.paletteRevision = m_nextId++;
    chunk.paletteDirty = false;
    ++m_paletteBuilds;
}

void ChunkSet::setHighlight(Highlight highlight)
{
    if (highlight == m_highlight) {
        return;
    }
    const bool selection =
        highlight.selected != m_highlight.selected || highlight.selectedOutline != m_highlight.selectedOutline;
    std::set<PickedItem> changed;
    addChanged(m_highlight.hovered, highlight.hovered, changed);
    addChanged(m_highlight.hoveredOutline, highlight.hoveredOutline, changed);
    if (selection) {
        addChanged(m_highlight.selected, highlight.selected, changed);
        addChanged(m_highlight.selectedOutline, highlight.selectedOutline, changed);
    }
    m_highlight = std::move(highlight);
    writeChanged(changed);
    buildHighlightGeometry(selection);
}

void ChunkSet::setHover(std::set<PickedItem> hovered, std::set<PickedItem> hoveredOutline)
{
    if (hovered == m_highlight.hovered && hoveredOutline == m_highlight.hoveredOutline) {
        return;
    }
    std::set<PickedItem> changed;
    addChanged(m_highlight.hovered, hovered, changed);
    addChanged(m_highlight.hoveredOutline, hoveredOutline, changed);
    m_highlight.hovered = std::move(hovered);
    m_highlight.hoveredOutline = std::move(hoveredOutline);
    writeChanged(changed);
    buildHighlightGeometry(false);
}

void ChunkSet::addChanged(const std::set<PickedItem>& before, const std::set<PickedItem>& after,
                          std::set<PickedItem>& changed)
{
    std::ranges::set_symmetric_difference(before, after, std::inserter(changed, changed.end()));
}

void ChunkSet::writeChanged(const std::set<PickedItem>& changed)
{
    std::set<Data*> written;
    for (const PickedItem& item : changed) {
        Data* owner = nullptr;
        if (Entry* entry = entryFor(item, &owner); entry != nullptr) {
            writeState(item, *entry);
            written.insert(owner);
        }
    }
    for (Data* chunk : written) {
        chunk->paletteRevision = m_nextId++;
    }
    ++m_highlightWrites;
}

void ChunkSet::buildHighlightGeometry(bool selectionChanged)
{
    const bool had =
        !m_lines.empty() || !m_selectedEdges.empty() || !m_selectedFaces.empty() || !m_hoveredFaces.empty();
    m_lines.clear();
    m_hoveredFaces.clear();
    using Body = std::pair<int, int>;
    const auto collect = [](const std::set<PickedItem>& items, PickKind kind,
                            std::map<Body, std::set<std::uint32_t>>& into) {
        for (const PickedItem& item : items) {
            if (item.kind == kind) {
                into[{item.feature, item.part}].insert(item.index);
            }
        }
    };
    std::map<Body, std::set<std::uint32_t>> lines;
    std::map<Body, std::set<std::uint32_t>> hoveredFaces;
    collect(m_highlight.hovered, PickKind::Edge, lines);
    collect(m_highlight.hoveredOutline, PickKind::Edge, lines);
    collect(m_highlight.hovered, PickKind::Face, hoveredFaces);
    std::erase_if(m_itemIndexes, [](const auto& entry) { return entry.second.mesh.expired(); });
    const auto meshOf = [this](const Body& body) -> std::shared_ptr<const snapshot::Mesh> {
        const auto found = m_owner.find({body.first, body.second, 0});
        if (found == m_owner.end()) {
            return nullptr;
        }
        const Slot& slot = found->second->members.at(found->first);
        return slot.visible ? slot.mesh : nullptr;
    };
    const auto addEdges = [&](const std::map<Body, std::set<std::uint32_t>>& wanted, std::vector<Segment>& into) {
        for (const auto& [body, ids] : wanted) {
            const std::shared_ptr<const snapshot::Mesh> mesh = meshOf(body);
            if (mesh == nullptr) {
                continue;
            }
            const ItemIndex& index = indexOf(body, mesh);
            for (const std::uint32_t id : ids) {
                if (id >= index.edgeAt.size() || index.edgeAt[id] == kNoEdge) {
                    continue;
                }
                const snapshot::Edge& edge = mesh->edges[index.edgeAt[id]];
                for (std::size_t i = 1; i < edge.points.size(); ++i) {
                    into.push_back(packSegment(relativePosition(edge.points[i - 1], m_origin),
                                               relativePosition(edge.points[i], m_origin), 0));
                }
            }
        }
    };
    const auto addFaces = [&](const std::map<Body, std::set<std::uint32_t>>& wanted, std::vector<Position>& into) {
        for (const auto& [body, ids] : wanted) {
            const std::shared_ptr<const snapshot::Mesh> mesh = meshOf(body);
            if (mesh == nullptr) {
                continue;
            }
            const ItemIndex& index = indexOf(body, mesh);
            for (const std::uint32_t id : ids) {
                if (id + 1 >= index.faceStart.size()) {
                    continue;
                }
                for (std::uint32_t at = index.faceStart[id]; at < index.faceStart[id + 1]; ++at) {
                    const snapshot::Triangle& triangle = mesh->triangles[index.faceTriangles[at]];
                    if (!valid(*mesh, triangle)) {
                        continue;
                    }
                    for (const std::uint32_t vertex : triangle) {
                        into.push_back(relativePosition(mesh->vertices[vertex], m_origin));
                    }
                }
            }
        }
    };
    if (selectionChanged) {
        std::map<Body, std::set<std::uint32_t>> selectedEdges;
        std::map<Body, std::set<std::uint32_t>> selectedFaces;
        m_selectedLineIds.clear();
        collect(m_highlight.selected, PickKind::Edge, m_selectedLineIds);
        collect(m_highlight.selectedOutline, PickKind::Edge, m_selectedLineIds);
        collect(m_highlight.selected, PickKind::Edge, selectedEdges);
        collect(m_highlight.selected, PickKind::Face, selectedFaces);
        m_selectedLines.clear();
        m_selectedEdges.clear();
        m_selectedFaces.clear();
        addEdges(m_selectedLineIds, m_selectedLines);
        addEdges(selectedEdges, m_selectedEdges);
        addFaces(selectedFaces, m_selectedFaces);
    }
    for (auto& [body, ids] : lines) {
        if (const auto selected = m_selectedLineIds.find(body); selected != m_selectedLineIds.end()) {
            std::erase_if(ids, [&](std::uint32_t id) { return selected->second.contains(id); });
        }
    }
    m_lines.assign(m_selectedLines.begin(), m_selectedLines.end());
    addEdges(lines, m_lines);
    addFaces(hoveredFaces, m_hoveredFaces);
    const bool has =
        !m_lines.empty() || !m_selectedEdges.empty() || !m_selectedFaces.empty() || !m_hoveredFaces.empty();
    if (had || has) {
        m_highlightRevision = m_nextId++;
    }
}

const ChunkSet::ItemIndex& ChunkSet::indexOf(std::pair<int, int> body,
                                             const std::shared_ptr<const snapshot::Mesh>& mesh)
{
    ItemIndex& index = m_itemIndexes[body];
    if (index.mesh.lock() == mesh) {
        return index;
    }
    index = {.mesh = mesh, .faceStart = {}, .faceTriangles = {}, .edgeAt = {}};
    const std::uint32_t faces = facesOf(*mesh);
    index.faceStart.assign(static_cast<std::size_t>(faces) + 1, 0);
    for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
        ++index.faceStart[faceAt(*mesh, i) + 1];
    }
    for (std::size_t face = 0; face < faces; ++face) {
        index.faceStart[face + 1] += index.faceStart[face];
    }
    index.faceTriangles.resize(mesh->triangles.size());
    std::vector<std::uint32_t> next(index.faceStart.begin(), index.faceStart.end() - 1);
    for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
        index.faceTriangles[next[faceAt(*mesh, i)]++] = static_cast<std::uint32_t>(i);
    }
    std::size_t edges = mesh->edgeCount;
    for (const snapshot::Edge& edge : mesh->edges) {
        edges = std::max<std::size_t>(edges, static_cast<std::size_t>(edge.id) + 1);
    }
    index.edgeAt.assign(edges, kNoEdge);
    for (std::size_t i = 0; i < mesh->edges.size(); ++i) {
        index.edgeAt[mesh->edges[i].id] = static_cast<std::uint32_t>(i);
    }
    return index;
}

Entry* ChunkSet::entryFor(const PickedItem& item, Data** chunk)
{
    const auto found = m_owner.find({item.feature, item.part, item.index / kPaletteEntries});
    if (found == m_owner.end()) {
        return nullptr;
    }
    Data& data = *found->second;
    const Slot& slot = data.members.at(found->first);
    std::vector<Entry>* palette = nullptr;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t base = 0;
    switch (item.kind) {
    case PickKind::None:
        return nullptr;
    case PickKind::Face:
        palette = &data.palette;
        first = slot.firstEntry;
        count = slot.entryCount;
        base = slot.firstFace;
        break;
    case PickKind::Edge:
        palette = &data.edgePalette;
        first = slot.firstEdgeEntry;
        count = slot.edgeEntryCount;
        base = slot.firstEdge;
        break;
    case PickKind::Point:
        palette = &data.pointPalette;
        first = slot.firstPointEntry;
        count = slot.pointEntryCount;
        base = slot.firstPoint;
        break;
    }
    const std::uint32_t offset = item.index - base;
    if (offset >= count || first + offset >= palette->size()) {
        return nullptr;
    }
    *chunk = &data;
    return &(*palette)[first + offset];
}

void ChunkSet::writeState(const PickedItem& item, Entry& entry) const
{
    if (item.kind == PickKind::Face) {
        const auto selected = m_highlight.selected.contains(item) ? kEntrySelected : std::uint8_t{0};
        const auto hovered = m_highlight.hovered.contains(item) ? kEntryHovered : std::uint8_t{0};
        entry[3] = static_cast<std::uint8_t>((entry[3] & kEntryVisible) | selected | hovered);
        return;
    }
    const bool selected = m_highlight.selected.contains(item) || m_highlight.selectedOutline.contains(item);
    const bool hovered = m_highlight.hovered.contains(item) || m_highlight.hoveredOutline.contains(item);
    const EdgeState state = selected && hovered ? EdgeState::SelectedHovered
                            : selected          ? EdgeState::Selected
                            : hovered           ? EdgeState::Hovered
                                                : EdgeState::Normal;
    entry[0] = static_cast<std::uint8_t>(state);
}

bool ChunkSet::edgesHighlighted() const
{
    const auto edge = [](const PickedItem& item) { return item.kind == PickKind::Edge; };
    return std::ranges::any_of(m_highlight.hovered, edge) || std::ranges::any_of(m_highlight.selected, edge) ||
           !m_highlight.hoveredOutline.empty() || !m_highlight.selectedOutline.empty();
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
    const bool highlightedOnly = !m_edges && edgesHighlighted();
    const bool edges = m_edges || highlightedOnly;
    DrawList list{.frame = frame,
                  .lit = m_lit,
                  .cullClosed = m_cullClosed,
                  .pixelRatio = m_pixelRatio,
                  .chunks = {},
                  .faces = m_drawn,
                  .edges = edges,
                  .highlightedEdgesOnly = highlightedOnly,
                  .edgeColor = m_edgeColor,
                  .edgeWidth = m_edgeWidth,
                  .highlight = {.revision = m_highlightRevision,
                                .lines = m_lines,
                                .selectedEdges = m_selectedEdges,
                                .selectedFaces = m_selectedFaces,
                                .hoveredFaces = m_hoveredFaces,
                                .hidden = m_drawn,
                                .wireFill = !m_drawn}};
    for (const auto& chunk : m_chunks) {
        if ((!m_drawn || chunk->indices.empty()) && (!edges || chunk->segments.empty())) {
            continue;
        }
        const bool shown = std::ranges::any_of(chunk->members, [](const auto& item) { return item.second.visible; });
        if (!shown) {
            continue;
        }
        list.chunks.push_back(chunkOf(*chunk));
    }
    return list;
}

DrawList ChunkSet::pickList(const Frame& frame) const
{
    DrawList list = draw(frame);
    list.edges = true;
    list.highlightedEdgesOnly = false;
    list.highlight = {};
    list.chunks.clear();
    for (const auto& chunk : m_chunks) {
        if (std::ranges::any_of(chunk->members, [](const auto& item) { return item.second.visible; })) {
            list.chunks.push_back(chunkOf(*chunk));
        }
    }
    return list;
}

Chunk ChunkSet::chunkOf(const Data& chunk) const
{
    return {.id = chunk.id,
            .meshRevision = chunk.meshRevision,
            .paletteRevision = chunk.paletteRevision,
            .closed = chunk.closed,
            .vertices = chunk.vertices,
            .indices = chunk.indices,
            .palette = chunk.palette,
            .segments = chunk.segments,
            .edgePalette = chunk.edgePalette,
            .points = chunk.points,
            .pointPalette = chunk.pointPalette};
}

std::optional<PickedItem> ChunkSet::itemOf(std::uint64_t chunk, PickKind kind, std::uint32_t entry) const
{
    const auto found = std::ranges::find_if(m_chunks, [chunk](const auto& data) { return data->id == chunk; });
    if (found == m_chunks.end()) {
        return std::nullopt;
    }
    for (const auto& [key, slot] : (*found)->members) {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        std::uint32_t base = 0;
        switch (kind) {
        case PickKind::None:
            return std::nullopt;
        case PickKind::Face:
            first = slot.firstEntry;
            count = slot.entryCount;
            base = slot.firstFace;
            break;
        case PickKind::Edge:
            first = slot.firstEdgeEntry;
            count = slot.edgeEntryCount;
            base = slot.firstEdge;
            break;
        case PickKind::Point:
            first = slot.firstPointEntry;
            count = slot.pointEntryCount;
            base = slot.firstPoint;
            break;
        }
        if (entry >= first && entry - first < count) {
            return PickedItem{
                .feature = std::get<0>(key), .part = std::get<1>(key), .kind = kind, .index = base + entry - first};
        }
    }
    return std::nullopt;
}

void ChunkSet::clear()
{
    m_chunks.clear();
    m_owner.clear();
    buildHighlightGeometry(true);
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
