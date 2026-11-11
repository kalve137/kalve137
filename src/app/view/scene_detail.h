// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "geometry/face_colors.h"
#include "viewer/style.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Quantity_Color.hxx>
#include <TopoDS_Shape.hxx>

#include <QColor>

namespace kalve137::app::scene_detail {

inline constexpr int kWholeObject = 0;
inline constexpr int kNotSelectable = -1;
inline constexpr double kOriginPlaneTransparency = viewer::style::kTintHalf;

Quantity_Color occt(const QColor& color);
Quantity_Color occt(const geometry::Color& color);
Handle(AIS_Shape) makeWire(const TopoDS_Shape& shape, const QColor& color, double width,
                           Aspect_TypeOfLine type = Aspect_TOL_SOLID);
Handle(AIS_Shape) makeSurface(const TopoDS_Shape& shape, const QColor& color, double transparency);
void hoverInOwnLayer(const Handle(AIS_InteractiveObject)& object, const Handle(AIS_InteractiveContext)& context);

} // namespace kalve137::app::scene_detail
