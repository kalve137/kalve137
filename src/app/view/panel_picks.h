// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "core/feature.h"
#include "core/recompute.h"
#include "render/chunk_set.h"
#include "render/draw_list.h"
#include "snapshot/color.h"

#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace kalve137::app {

using ShapeIndex = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

struct BodyShapes {
    TopoDS_Shape shape;
    ShapeIndex faces;
    ShapeIndex edges;
    ShapeIndex vertices;
    std::map<int, std::vector<std::uint32_t>> outlines;
};

class BodyShapeCache {
public:
    BodyShapes& of(const core::Body& body);

private:
    std::map<core::BodyKey, BodyShapes> m_bodies;
};

void addOutline(const core::Body& body, std::uint32_t face, BodyShapeCache& shapes,
                std::set<render::PickedItem>& outline);
struct VertexColors {
    snapshot::Color hovered;
    snapshot::Color selected;
};

std::optional<camera::Vec3> vertexAt(const core::Body& body, std::uint32_t vertex, BodyShapeCache& shapes);
void addVertexMark(const core::Body& body, std::uint32_t vertex, bool selected, const VertexColors& colors,
                   BodyShapeCache& shapes, std::vector<render::MarkerItem>& marks);
void addWholeBody(const core::Body& body, BodyShapeCache& shapes, std::set<render::PickedItem>& faces,
                  std::set<render::PickedItem>& edges);

struct PanelPicks {
    std::vector<TopoDS_Edge> edges;
    std::vector<TopoDS_Face> faces;
    std::vector<core::BodyKey> bodies;
    std::vector<TopoDS_Vertex> vertices;

    bool empty() const { return edges.empty() && faces.empty() && bodies.empty() && vertices.empty(); }
};

struct PanelItems {
    std::set<render::PickedItem> selected;
    std::set<render::PickedItem> selectedOutline;
    std::set<std::pair<int, int>> tinted;

    bool operator==(const PanelItems&) const = default;
};

PanelItems panelItems(const PanelPicks& picks, std::span<const core::Body* const> shown, BodyShapeCache& shapes);

} // namespace kalve137::app
