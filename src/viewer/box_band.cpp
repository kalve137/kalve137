// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/box_band.h"

#include "viewer/style.h"

#include <Aspect_TypeOfLine.hxx>
#include <Aspect_Window.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <NCollection_Vec2.hxx>

#include <algorithm>
#include <cmath>

namespace kalve137::viewer {

namespace {

NCollection_Vec2<int> toBacking(const Handle(V3d_View)& view, QPointF pos)
{
    const NCollection_Vec2<double> backing =
        view->Window()->ConvertPointToBacking(NCollection_Vec2<double>(pos.x(), pos.y()));
    return NCollection_Vec2<int>(static_cast<int>(std::lround(backing.x())),
                                 static_cast<int>(std::lround(backing.y())));
}

} // namespace

bool isCrossingBox(QPointF from, QPointF to)
{
    return to.x() < from.x();
}

bool isBoxDrag(QPointF from, QPointF to)
{
    const QPointF delta = to - from;
    return std::max(std::abs(delta.x()), std::abs(delta.y())) >= kBoxStartPixels;
}

void BoxBand::show(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& view, QPointF from,
                   QPointF to)
{
    if (m_band.IsNull()) {
        m_band = new AIS_RubberBand(m_color, Aspect_TOL_SOLID, m_color, style::kTintFaint, style::kStrokeHairline);
        m_band->SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_2d, Aspect_TOTP_LEFT_UPPER));
        m_band->SetZLayer(Graphic3d_ZLayerId_TopOSD);
        m_band->SetDisplayMode(0);
        m_band->SetMutable(true);
    }
    const NCollection_Vec2<int> a = toBacking(view, from);
    const NCollection_Vec2<int> b = toBacking(view, to);
    m_band->SetLineColor(m_color);
    m_band->SetFilling(m_color, style::kTintFaint);
    m_band->SetLineType(isCrossingBox(from, to) ? Aspect_TOL_DASH : Aspect_TOL_SOLID);
    m_band->SetRectangle(std::min(a.x(), b.x()), -std::max(a.y(), b.y()), std::max(a.x(), b.x()),
                         -std::min(a.y(), b.y()));
    if (!m_shown) {
        context->Display(m_band, 0, -1, false);
        m_shown = true;
    } else {
        context->Redisplay(m_band, false);
    }
}

void BoxBand::hide(const Handle(AIS_InteractiveContext)& context)
{
    if (m_shown) {
        context->Remove(m_band, false);
        m_shown = false;
    }
}

} // namespace kalve137::viewer
