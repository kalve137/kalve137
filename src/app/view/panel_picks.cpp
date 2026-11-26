// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/panel_picks.h"

#include <BRep_Tool.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>

namespace kalve137::app {

namespace {

render::PickedItem itemOf(const core::Body& body, render::PickKind kind, int index)
{
    return {.feature = body.id, .part = body.part, .kind = kind, .index = static_cast<std::uint32_t>(index - 1)};
}

} // namespace

BodyShapes& BodyShapeCache::of(const core::Body& body)
{
    BodyShapes& shapes = m_bodies[body.key()];
    if (!shapes.shape.IsEqual(body.shape)) {
        shapes = {};
        shapes.shape = body.shape;
        TopExp::MapShapes(body.shape, TopAbs_FACE, shapes.faces);
        TopExp::MapShapes(body.shape, TopAbs_EDGE, shapes.edges);
        TopExp::MapShapes(body.shape, TopAbs_VERTEX, shapes.vertices);
    }
    return shapes;
}

void addOutline(const core::Body& body, std::uint32_t face, BodyShapeCache& shapes,
                std::set<render::PickedItem>& outline)
{
    BodyShapes& indexed = shapes.of(body);
    const auto index = static_cast<int>(face) + 1;
    if (index > indexed.faces.Extent()) {
        return;
    }
    const auto [edges, added] = indexed.outlines.try_emplace(index);
    if (added) {
        for (TopExp_Explorer edge(indexed.faces.FindKey(index), TopAbs_EDGE); edge.More(); edge.Next()) {
            if (const int found = indexed.edges.FindIndex(edge.Current()); found > 0) {
                edges->second.push_back(static_cast<std::uint32_t>(found - 1));
            }
        }
    }
    for (const std::uint32_t edge : edges->second) {
        outline.insert({.feature = body.id, .part = body.part, .kind = render::PickKind::Edge, .index = edge});
    }
}

std::optional<camera::Vec3> vertexAt(const core::Body& body, std::uint32_t vertex, BodyShapeCache& shapes)
{
    const BodyShapes& indexed = shapes.of(body);
    const auto index = static_cast<int>(vertex) + 1;
    if (index > indexed.vertices.Extent()) {
        return std::nullopt;
    }
    const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(indexed.vertices.FindKey(index)));
    if (!std::isfinite(point.X()) || !std::isfinite(point.Y()) || !std::isfinite(point.Z())) {
        return std::nullopt;
    }
    return camera::Vec3{point.X(), point.Y(), point.Z()};
}

void addVertexMark(const core::Body& body, std::uint32_t vertex, bool selected, const VertexColors& colors,
                   BodyShapeCache& shapes, std::vector<render::MarkerItem>& marks)
{
    const std::optional<camera::Vec3> at = vertexAt(body, vertex, shapes);
    if (!at) {
        return;
    }
    marks.push_back({.anchor = *at,
                     .shape = selected ? render::MarkerShape::Ball : render::MarkerShape::Ring,
                     .scale = render::MarkerScale::Small,
                     .color = selected ? colors.selected : colors.hovered,
                     .flags = render::kMarkerTopmost,
                     .pick = 0});
}

void addWholeBody(const core::Body& body, BodyShapeCache& shapes, std::set<render::PickedItem>& faces,
                  std::set<render::PickedItem>& edges)
{
    const BodyShapes& indexed = shapes.of(body);
    for (int index = 1; index <= indexed.faces.Extent(); ++index) {
        faces.insert(itemOf(body, render::PickKind::Face, index));
    }
    for (int index = 1; index <= indexed.edges.Extent(); ++index) {
        edges.insert(itemOf(body, render::PickKind::Edge, index));
    }
}

PanelItems panelItems(const PanelPicks& picks, std::span<const core::Body* const> shown, BodyShapeCache& shapes)
{
    PanelItems items;
    if (picks.empty()) {
        return items;
    }
    for (const core::Body* body : shown) {
        if (body == nullptr || body->shape.IsNull()) {
            continue;
        }
        const BodyShapes& indexed = shapes.of(*body);
        for (const TopoDS_Edge& edge : picks.edges) {
            if (const int index = indexed.edges.FindIndex(edge); index > 0) {
                items.selected.insert(itemOf(*body, render::PickKind::Edge, index));
            }
        }
        for (const TopoDS_Face& face : picks.faces) {
            if (const int index = indexed.faces.FindIndex(face); index > 0 && picks.openFaces) {
                items.opened.insert(itemOf(*body, render::PickKind::Face, index));
            } else if (index > 0) {
                items.selected.insert(itemOf(*body, render::PickKind::Face, index));
                addOutline(*body, static_cast<std::uint32_t>(index - 1), shapes, items.selectedOutline);
            }
        }
        for (const TopoDS_Vertex& vertex : picks.vertices) {
            if (const int index = indexed.vertices.FindIndex(vertex); index > 0) {
                items.selected.insert(itemOf(*body, render::PickKind::Point, index));
            }
        }
        if (std::ranges::find(picks.bodies, body->key()) != picks.bodies.end()) {
            items.tinted.emplace(body->id, body->part);
        }
    }
    return items;
}

} // namespace kalve137::app
