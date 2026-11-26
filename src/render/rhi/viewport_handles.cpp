// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/viewport.h"

#include <utility>

namespace kalve137::render {

void Viewport::setHandles(std::vector<HandleItem> handles)
{
    if (handles == m_handles) {
        return;
    }
    m_handles = std::move(handles);
    refreshHandles();
}

void Viewport::setMeasure(MeasureItems measure)
{
    if (measure == m_measure) {
        return;
    }
    const bool markersChanged = measure.markers != m_measure.markers;
    m_measure = std::move(measure);
    if (markersChanged) {
        refreshMarkers();
    }
    refreshHandles();
}

HandleDraw Viewport::handleDraw() const
{
    return {.revision = m_handleRevision, .arrows = m_handleGeometry.arrows, .guides = m_handleGeometry.guides};
}

void Viewport::refreshHandles()
{
    HandleGeometry geometry = placeHandles(m_handles, m_scene.origin);
    std::vector<DatumLine> measure = placeMeasureLines(m_measure, m_scene.origin);
    geometry.guides.insert(geometry.guides.end(), measure.begin(), measure.end());
    if (geometry != m_handleGeometry) {
        m_handleGeometry = std::move(geometry);
        ++m_handleRevision;
    }
    update();
}

} // namespace kalve137::render
