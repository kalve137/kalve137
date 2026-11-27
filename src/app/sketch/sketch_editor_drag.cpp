// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_editor.h"

#include "app/sketch/sketch_editor_detail.h"
#include "app/theme/theme.h"
#include "app/view/band_overlay.h"
#include "log/log.h"
#include "sketch/dimension.h"
#include "sketch/dimension_layout.h"
#include "sketch/profiles.h"
#include "sketch/select.h"
#include "viewer/box_band.h"
#include "viewer/occt_view_widget.h"

#include <PrsDim_Dimension.hxx>

#include <QPointF>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>
namespace kalve137::app {

using namespace sketch_editor_detail;

std::optional<QPointF> SketchEditor::labelOnScreen(Id constraint) const
{
    if (m_projection.hasCameraInput()) {
        const sketch::Sketch& s = shownSketch();
        const sketch::Constraint* c = s.constraint(constraint);
        const std::optional<sketch::DimensionLayout> layout =
            c != nullptr ? sketch::layoutDimension(s, *c, worldPerPixel(), sketch::dimensionMiddle(s)) : std::nullopt;
        if (!layout) {
            return std::nullopt;
        }
        return toScreen(sketch::toWorld(plane(), layout->text));
    }
    for (const auto& [object, id] : m_constraintObjects) {
        if (const auto* dimension = dynamic_cast<const PrsDim_Dimension*>(object);
            dimension != nullptr && id == constraint) {
            return m_view->toScreen(dimension->GetTextPosition());
        }
    }
    return std::nullopt;
}

void SketchEditor::startLabelDrag(Id constraint, QPointF pos, Vec2 at)
{
    const sketch::Constraint* c = current().constraint(constraint);
    if (c == nullptr || !sketch::isDimension(c->kind)) {
        return;
    }
    const std::optional<QPointF> screen = labelOnScreen(constraint);
    const std::optional<Vec2> label = screen ? toSketch(*screen) : std::nullopt;
    if (!label) {
        return;
    }
    m_dragLabel = constraint;
    m_dragLabelPress = pos;
    m_dragLabelGrab = at;
    m_dragLabelStart = *label;
    m_draggingLabel = false;
    m_dragStart = current();
    m_dragPreview.reset();
}

void SketchEditor::dragLabel(QPointF pos, Vec2 at)
{
    if (!m_draggingLabel && !viewer::isBoxDrag(m_dragLabelPress, pos)) {
        return;
    }
    m_draggingLabel = true;
    sketch::Sketch candidate = *m_dragStart;
    sketch::Constraint* c = candidate.constraint(m_dragLabel);
    const std::optional<Vec2> anchor = c != nullptr ? sketch::dimensionAnchor(candidate, *c) : std::nullopt;
    if (!anchor) {
        return;
    }
    c->labelOffset = m_dragLabelStart + (at - m_dragLabelGrab) - *anchor;
    m_dragPreview = std::move(candidate);
    redraw();
}

void SketchEditor::dropDrag()
{
    m_dragPoints.clear();
    m_draggingGroup = false;
    m_dragLabel = 0;
    m_draggingLabel = false;
    m_dragStart.reset();
    m_dragPreview.reset();
    dropBox();
}

void SketchEditor::dropBox()
{
    m_boxStart.reset();
    if (std::exchange(m_boxing, false)) {
        if (m_band != nullptr) {
            m_band->hideBand();
        } else {
            m_view->hideBand();
        }
    }
}

void SketchEditor::selectInBox(QPointF from, QPointF to)
{
    const gp_Ax3 ax = plane();
    const auto project = [&](Vec2 p) {
        const QPointF screen = toScreen(sketch::toWorld(ax, p));
        return Vec2{screen.x(), screen.y()};
    };
    const sketch::BoxMode mode = viewer::isCrossingBox(from, to) ? sketch::BoxMode::Crossing : sketch::BoxMode::Window;
    const std::vector<Id> ids = sketch::inBox(current(), project, {from.x(), from.y()}, {to.x(), to.y()}, mode);
    m_selection.insert(ids.begin(), ids.end());
    K137_DEBUG(Input, "{} box picked {} sketch items", mode == sketch::BoxMode::Crossing ? "Crossing" : "Window",
               ids.size());
    redraw();
    emit stateChanged();
}

} // namespace kalve137::app
