// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/occt_view_widget.h"

#include "log/log.h"

#include <Standard_WarningsDisable.hxx>
#include <QMetaObject>
#include <QMouseEvent>
#include <Standard_WarningsRestore.hxx>

#include <Aspect_Window.hxx>
#include <Graphic3d_Camera.hxx>
#include <NCollection_Vec2.hxx>
#include <Precision.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace kalve137::viewer {

gp_Dir OcctViewWidget::towardViewer(const gp_Pnt& point) const
{
    const Handle(Graphic3d_Camera)& camera = m_view->Camera();
    const gp_Dir back = camera->Direction().Reversed();
    if (camera->IsOrthographic()) {
        return back;
    }
    const gp_Vec toEye(point, camera->Eye());
    return toEye.Magnitude() > Precision::Confusion() ? gp_Dir(toEye) : back;
}

void OcctViewWidget::setBoxSelection(BoxFilter filter)
{
    m_boxFilter = std::move(filter);
    if (!m_boxFilter) {
        m_boxStart.reset();
        m_boxing = false;
        hideBand();
    }
}

void OcctViewWidget::showBand(QPointF from, QPointF to)
{
    if (isReady()) {
        m_band.show(m_context, m_view, from, to);
        updateView();
    }
}

void OcctViewWidget::hideBand()
{
    if (m_band.isShown()) {
        m_band.hide(m_context);
        updateView();
    }
}

bool OcctViewWidget::startBox(const QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    m_selectedByBox = false;
    m_boxStart.reset();
    m_boxing = false;
    if (!m_boxFilter || !isReady() || isViewCubeHovered()) {
        return false;
    }
    detectAt(event->position());
    if (m_context->HasDetected()) {
        return false;
    }
    m_boxStart = event->position();
    m_boxModifiers = event->modifiers();
    return true;
}

bool OcctViewWidget::dragBox(const QMouseEvent* event)
{
    if (!m_boxStart || !(event->buttons() & Qt::LeftButton)) {
        return false;
    }
    if (!m_boxing && !isBoxDrag(*m_boxStart, event->position())) {
        return false;
    }
    m_boxing = true;
    showBand(*m_boxStart, event->position());
    return true;
}

bool OcctViewWidget::finishBox(const QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_boxStart) {
        return false;
    }
    const QPointF from = *std::exchange(m_boxStart, std::nullopt);
    if (!std::exchange(m_boxing, false)) {
        return false;
    }
    hideBand();
    ResetViewInput();
    const QPointF to = event->position();
    const bool crossing = isCrossingBox(from, to);
    const auto backing = [this](QPointF pos) {
        const NCollection_Vec2<double> point =
            m_view->Window()->ConvertPointToBacking(NCollection_Vec2<double>(pos.x(), pos.y()));
        return NCollection_Vec2<int>(static_cast<int>(std::lround(point.x())),
                                     static_cast<int>(std::lround(point.y())));
    };
    const NCollection_Vec2<int> a = backing(from);
    const NCollection_Vec2<int> b = backing(to);
    const bool additive = (m_boxModifiers & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
    m_context->MainSelector()->AllowOverlapDetection(crossing);
    m_context->SelectRectangle(NCollection_Vec2<int>(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                               NCollection_Vec2<int>(std::max(a.x(), b.x()), std::max(a.y(), b.y())), m_view,
                               additive ? AIS_SelectionScheme_Add : AIS_SelectionScheme_Replace);
    m_context->MainSelector()->AllowOverlapDetection(false);
    std::vector<Handle(SelectMgr_EntityOwner)> owners;
    for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
        owners.push_back(m_context->SelectedOwner());
    }
    const std::vector<bool> keep = m_boxFilter(owners);
    for (std::size_t i = 0; i < owners.size(); ++i) {
        if (i >= keep.size() || !keep[i]) {
            m_context->AddOrRemoveSelected(owners[i], false);
        }
    }
    m_selectedByBox = true;
    K137_DEBUG(Input, "{} box selected {}", crossing ? "Crossing" : "Window", m_context->NbSelected());
    QMetaObject::invokeMethod(this, &OcctViewWidget::selectionChanged, Qt::QueuedConnection);
    redraw();
    return true;
}

} // namespace kalve137::viewer
