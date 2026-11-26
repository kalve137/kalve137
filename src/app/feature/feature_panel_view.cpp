// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"
#include "app/feature/feature_panel.h"
#include "app/session/design_session.h"
#include "app/view/body_opacity.h"
#include "app/view/panel_picks.h"
#include "app/view/sketch_snapshots.h"
#include "app/view/view_projection.h"
#include "app/view/viewport_picker.h"
#include "render/handles.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"

#include <TopoDS_Shape.hxx>

#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace kalve137::app {

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
    picker->showModel(m_baseModel);
    viewport->setPickScene(m_baseSnapshots.scene(m_baseModel, visible));
    viewport->setScene(withBodyOpacity(std::move(scene), m_scene.pickMode()));
    viewport->setSketches(sketchSnapshots(document, model, m_session));
}

void FeaturePanel::endPreview()
{
    m_previewSnapshots.clear();
    m_baseSnapshots.clear();
    if (render::Viewport* viewport = m_projection.viewport()) {
        viewport->setPickScene(std::nullopt);
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
        picker->setPanelPicks({.edges = resolved.edges, .faces = resolved.faces, .bodies = std::move(keys)});
    }
    m_scene.setHighlightedProfiles(resolved.profiles);
    m_scene.setHighlightedEdges(resolved.edges);
    m_scene.setHighlightedFaces(resolved.faces);
    m_scene.setHighlightedBodies(std::move(shapes));
}

} // namespace kalve137::app
