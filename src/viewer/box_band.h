// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_RubberBand.hxx>
#include <Quantity_Color.hxx>
#include <V3d_View.hxx>

#include <QPointF>
#include <QRectF>

namespace kalve137::viewer {

inline constexpr double kBoxStartPixels = 5.0;

bool isCrossingBox(QPointF from, QPointF to);
bool isBoxDrag(QPointF from, QPointF to);

class BoxBand {
public:
    void setColor(const Quantity_Color& color) { m_color = color; }
    void show(const Handle(AIS_InteractiveContext)& context, const Handle(V3d_View)& view, QPointF from, QPointF to);
    void hide(const Handle(AIS_InteractiveContext)& context);
    bool isShown() const { return m_shown; }

private:
    Handle(AIS_RubberBand) m_band;
    Quantity_Color m_color;
    bool m_shown = false;
};

} // namespace kalve137::viewer
