// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/occt_view_widget.h"

#include <Graphic3d_Camera.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <optional>

namespace kalve137::viewer {

void OcctViewWidget::setGridStyle(const GridStyle& style)
{
    m_grid->setStyle(style);
    m_gridWash->setStyle(style);
    updateGrid(true);
}

void OcctViewWidget::setGridVisible(bool visible)
{
    if (m_gridVisible == visible) {
        return;
    }
    m_gridVisible = visible;
    emit gridChanged();
    if (!isReady()) {
        return;
    }
    if (visible) {
        if (!m_gridExtent) {
            m_context->Display(m_gridWash, 0, -1, false);
        }
        m_context->Display(m_grid, 0, -1, false);
        updateGrid(true);
    } else {
        m_context->Erase(m_gridWash, false);
        m_context->Erase(m_grid, false);
    }
    redraw();
}

double OcctViewWidget::gridStep() const
{
    return m_gridVisible ? m_grid->step() : 0.0;
}

void OcctViewWidget::setGridLabels(bool labels)
{
    m_grid->setLabels(labels);
    updateGrid(true);
}

void OcctViewWidget::setGridSpacing(bool adaptive, double spacing)
{
    if (m_gridAdaptive == adaptive && m_gridSpacing == spacing) {
        return;
    }
    m_gridAdaptive = adaptive;
    m_gridSpacing = spacing;
    emit gridChanged();
    updateGrid(true);
}

void OcctViewWidget::setGridPlane(const gp_Ax3& plane)
{
    m_grid->setPlane(plane);
    m_gridWash->setPlane(plane);
    emit gridChanged();
    updateGrid(true);
}

void OcctViewWidget::setGridExtent(const std::optional<GridExtent>& extent)
{
    const bool washChanged = extent.has_value() != m_gridExtent.has_value();
    m_gridExtent = extent;
    emit gridChanged();
    if (washChanged && m_gridVisible && isReady()) {
        if (extent) {
            m_context->Erase(m_gridWash, false);
        } else {
            m_context->Display(m_gridWash, 0, -1, false);
        }
    }
    updateGrid(true);
}

void OcctViewWidget::updateGrid(bool force)
{
    if (!m_gridVisible || !isReady()) {
        return;
    }
    const double worldPerLogicalPixel = worldPerPixel();
    const gp_Dir normal = m_grid->plane().Direction();
    const double away = normal.Dot(m_view->Camera()->Direction()) < 0.0 ? -1.0 : 1.0;
    gp_Trsf behind;
    behind.SetTranslation(gp_Vec(normal) * (away * render::kGridDepthPixels * worldPerLogicalPixel));
    if (!behind.TranslationPart().IsEqual(m_grid->LocalTransformation().TranslationPart(), 0.0)) {
        m_context->SetLocation(m_grid, behind);
    }
    const double step =
        render::gridStep(m_gridAdaptive, m_gridSpacing, worldPerLogicalPixel * render::kGridMinCellPixels);
    const double viewRadius = 0.5 * std::hypot(width(), height()) * worldPerLogicalPixel * render::kGridRadiusFactor;
    const gp_Pnt center = m_view->Camera()->Center();
    std::optional<render::GridExtent> extent;
    if (m_gridExtent) {
        extent = gridExtentOf(*m_gridExtent);
    }
    const render::GridLayout wanted = render::layoutGrid(gridPlaneOf(m_grid->plane()), step,
                                                         {center.X(), center.Y(), center.Z()}, viewRadius, extent);
    if (!force && !render::isGridStale(m_grid->layout(), wanted)) {
        return;
    }
    m_grid->setLayout(wanted);
    m_gridWash->setLayout(wanted);
    m_context->Redisplay(m_grid, false);
    m_context->Redisplay(m_gridWash, false);
    m_view->Invalidate();
    updateView();
}

} // namespace kalve137::viewer
