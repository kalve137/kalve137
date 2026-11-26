// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"
#include "app/feature/feature_panel.h"
#include "app/view/view_projection.h"
#include "render/handles.h"
#include "render/rhi/viewport.h"

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

} // namespace kalve137::app
