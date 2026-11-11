// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

class BRepTools_History;

namespace kalve137::geometry {

TopoDS_Shape makeBox(double dx, double dy, double dz);
TopoDS_Shape makeCylinder(const gp_Pnt& baseCenter, double radius, double height);
TopoDS_Shape cut(const TopoDS_Shape& base, const TopoDS_Shape& tool, BRepTools_History* history = nullptr);
double volume(const TopoDS_Shape& shape);
double area(const TopoDS_Shape& shape);
gp_Pnt areaCentroid(const TopoDS_Shape& shape);
double length(const TopoDS_Shape& shape);

} // namespace kalve137::geometry
