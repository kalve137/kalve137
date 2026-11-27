// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"
#include "app/feature/feature_panel.h"
#include "app/session/design_session.h"
#include "app/theme/theme.h"
#include "app/view/body_opacity.h"
#include "app/view/panel_picks.h"
#include "app/view/sketch_snapshots.h"
#include "app/view/view_projection.h"
#include "app/view/viewport_picker.h"
#include "geometry/body_mesh.h"
#include "render/handles.h"
#include "render/outlines.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"
#include "viewer/style.h"

#include <QColor>

#include <TopoDS_Shape.hxx>

#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

snapshot::Color colorOf(const QColor& color)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF())};
}

std::shared_ptr<const snapshot::Body> toolGhost(const core::FeatureTool& tool)
{
    snapshot::Body ghost;
    ghost.feature = tool.feature;
    ghost.mesh = std::make_shared<const snapshot::Mesh>(geometry::meshBody(tool.shape));
    ghost.appearance.color = colorOf(theme::palette().viewport.previewCut);
    ghost.ghost = true;
    return std::make_shared<const snapshot::Body>(std::move(ghost));
}

std::vector<render::OutlineItem> capEdges(const core::FeatureTool& tool)
{
    const snapshot::Color color = colorOf(theme::palette().viewport.bodyEdge);
    std::vector<render::OutlineItem> edges;
    for (const TopoDS_Shape& cap : tool.caps) {
        for (snapshot::Polyline& line : geometry::edgeLines(cap)) {
            edges.push_back({.points = std::move(line), .color = color, .width = viewer::style::kStrokeRegular});
        }
    }
    return edges;
}

} // namespace

void FeaturePanel::publishHandles()
{
    render::Viewport* viewport = m_projection.viewport();
    if (viewport == nullptr) {
        return;
    }
    std::vector<render::HandleItem> items;
    for (const DistanceHandle* handle : {m_handle, m_secondHandle, m_thirdHandle}) {
        if (std::optional<render::HandleItem> item = handle->item()) {
            items.push_back(std::move(*item));
        }
    }
    viewport->setHandles(std::move(items));
}

void FeaturePanel::showPreview(const core::Document& document, const core::Model& model)
{
    render::Viewport* viewport = m_projection.viewport();
    auto* picker = qobject_cast<ViewportPicker*>(&m_source);
    if (viewport == nullptr || picker == nullptr) {
        return;
    }
    const auto visible = [this](core::BodyKey key) { return m_session.isBodyVisible(key); };
    for (core::SnapshotCache* cache : {&m_previewSnapshots, &m_baseSnapshots}) {
        if (cache->size() == 0) {
            cache->seedFrom(m_session.snapshots());
        }
    }
    snapshot::Scene scene = m_previewSnapshots.scene(model, visible);
    const std::set<core::BodyKey> ghosts = previewBodies(model);
    for (std::vector<std::shared_ptr<const snapshot::Body>>* bodies : {&scene.bodies, &scene.hidden}) {
        for (std::shared_ptr<const snapshot::Body>& body : *bodies) {
            if (ghosts.contains(core::BodyKey{.feature = body->feature, .part = body->part})) {
                snapshot::Body ghost = *body;
                ghost.ghost = true;
                body = std::make_shared<const snapshot::Body>(std::move(ghost));
            }
        }
    }
    const auto* extrude = std::get_if<core::ExtrudeFeature>(&*m_data);
    const bool tooled = extrude != nullptr && model.tool.feature != 0 && !model.tool.shape.IsNull();
    if (tooled && extrude->operation == core::Operation::Cut) {
        scene.bodies.push_back(toolGhost(model.tool));
    }
    picker->showModel(m_baseModel);
    viewport->setPickScene(m_baseSnapshots.scene(m_baseModel, visible));
    viewport->setScene(withBodyOpacity(std::move(scene), m_scene.pickMode()));
    viewport->setPreviewEdges(tooled ? capEdges(model.tool) : std::vector<render::OutlineItem>{});
    viewport->setSketches(sketchSnapshots(document, model, m_session));
}

void FeaturePanel::endPreview()
{
    m_previewSnapshots.clear();
    m_baseSnapshots.clear();
    if (render::Viewport* viewport = m_projection.viewport()) {
        viewport->setPickScene(std::nullopt);
        viewport->setPreviewEdges({});
    }
}

void FeaturePanel::showPicks(const Resolved& resolved, const std::vector<const core::Body*>& bodies)
{
    std::vector<TopoDS_Shape> shapes;
    std::vector<core::BodyKey> keys;
    for (const core::Body* body : bodies) {
        shapes.push_back(body->shape);
        keys.push_back(body->key());
    }
    if (auto* picker = qobject_cast<ViewportPicker*>(&m_source)) {
        picker->setPanelPicks({.edges = resolved.edges,
                               .faces = resolved.faces,
                               .bodies = std::move(keys),
                               .openFaces = std::holds_alternative<core::ShellFeature>(*m_data)});
    }
    m_scene.setHighlightedProfiles(resolved.profiles);
    m_scene.setHighlightedEdges(resolved.edges);
    m_scene.setHighlightedFaces(resolved.faces);
    m_scene.setHighlightedBodies(std::move(shapes));
}

} // namespace kalve137::app
