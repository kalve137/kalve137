// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/occt_view_widget.h"

#include <Graphic3d_Camera.hxx>

#include <cmath>

namespace kalve137::viewer {

namespace {

constexpr double kMinGridCellPixels = 12.0;
constexpr double kGridRadiusFactor = 2.5;

} // namespace

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
    updateGrid(true);
}

void OcctViewWidget::setGridPlane(const gp_Ax3& plane)
{
    m_grid->setPlane(plane);
    m_gridWash->setPlane(plane);
    updateGrid(true);
}

void OcctViewWidget::setGridExtent(const std::optional<GridExtent>& extent)
{
    const bool washChanged = extent.has_value() != m_gridExtent.has_value();
    m_gridExtent = extent;
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
    const double step = viewer::gridStep(m_gridAdaptive, m_gridSpacing, worldPerLogicalPixel * kMinGridCellPixels);
    const double viewRadius = 0.5 * std::hypot(width(), height()) * worldPerLogicalPixel * kGridRadiusFactor;
    const GridLayout wanted = layoutGrid(m_grid->plane(), step, m_view->Camera()->Center(), viewRadius, m_gridExtent);
    if (!force && !isGridStale(m_grid->layout(), wanted)) {
        return;
    }
    m_grid->setLayout(wanted.step, wanted.center, wanted.radius);
    m_gridWash->setLayout(wanted.step, wanted.center, wanted.radius);
    m_context->Redisplay(m_grid, false);
    m_context->Redisplay(m_gridWash, false);
    m_view->Invalidate();
    updateView();
}

} // namespace kalve137::viewer
