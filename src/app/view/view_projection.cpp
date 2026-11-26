// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/view_projection.h"

#include "render/rhi/viewport.h"
#include "viewer/occt_view_widget.h"

#include <QWidget>

#include <cmath>

namespace kalve137::app {

ViewProjection::ViewProjection(viewer::OcctViewWidget& view, render::Viewport* viewport, QObject* parent)
    : QObject(parent), m_view(view), m_viewport(viewport), m_cameraInput(viewport != nullptr)
{
    if (viewport != nullptr) {
        connect(viewport, &render::Viewport::cameraChanged, this, &ViewProjection::changed);
    } else {
        connect(&view, &viewer::OcctViewWidget::rendered, this, &ViewProjection::changed);
    }
}

QWidget* ViewProjection::canvas() const
{
    return m_viewport != nullptr ? static_cast<QWidget*>(m_viewport) : &m_view;
}

std::optional<ViewProjection::Projection> ViewProjection::projection() const
{
    if (m_viewport != nullptr) {
        const camera::Viewport viewport = m_viewport->viewport();
        if (!viewport.isValid() || !m_viewport->camera().isUsable()) {
            return std::nullopt;
        }
        return Projection{m_viewport->camera(), viewport};
    }
    const std::optional<camera::Viewport> viewport = m_view.viewport();
    if (!viewport) {
        return std::nullopt;
    }
    const camera::Camera camera = m_view.currentCamera();
    if (!camera.isUsable()) {
        return std::nullopt;
    }
    return Projection{camera, *viewport};
}

double ViewProjection::worldPerPixel() const
{
    if (!m_cameraInput) {
        return m_view.worldPerPixel();
    }
    const std::optional<Projection> shown = projection();
    const double wpp = shown ? shown->camera.worldPerPixel(shown->viewport) : 0.0;
    return std::isfinite(wpp) && wpp > 0.0 ? wpp : 0.0;
}

QPointF ViewProjection::toScreen(const gp_Pnt& point) const
{
    if (!m_cameraInput) {
        return m_view.toScreen(point);
    }
    const std::optional<Projection> shown = projection();
    if (!shown) {
        return {};
    }
    const camera::ScreenPoint screen = shown->camera.project({point.X(), point.Y(), point.Z()}, shown->viewport);
    return {screen.x, screen.y};
}

} // namespace kalve137::app
