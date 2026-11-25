// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"
#include "core/recompute.h"
#include "render/chunk_set.h"

#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <map>
#include <set>
#include <span>
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
void addWholeBody(const core::Body& body, BodyShapeCache& shapes, std::set<render::PickedItem>& faces,
                  std::set<render::PickedItem>& edges);

struct PanelPicks {
    std::vector<TopoDS_Edge> edges;
    std::vector<TopoDS_Face> faces;
    std::vector<core::BodyKey> bodies;

    bool empty() const { return edges.empty() && faces.empty() && bodies.empty(); }
};

struct PanelItems {
    std::set<render::PickedItem> selected;
    std::set<render::PickedItem> selectedOutline;
    std::set<render::PickedItem> bodies;
    std::set<render::PickedItem> bodyOutline;

    bool operator==(const PanelItems&) const = default;
};

PanelItems panelItems(const PanelPicks& picks, std::span<const core::Body* const> shown, BodyShapeCache& shapes);

} // namespace kalve137::app
