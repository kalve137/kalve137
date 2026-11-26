// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"
#include "app/feature/feature_panel.h"
#include "app/view/panel_picks.h"
#include "app/view/view_projection.h"
#include "app/view/viewport_picker.h"
#include "render/handles.h"
#include "render/rhi/viewport.h"

#include <TopoDS_Shape.hxx>

#include <optional>
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
