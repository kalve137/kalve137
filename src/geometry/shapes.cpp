// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/shapes.h"

#include "geometry/shape_history.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>

#include <stdexcept>

namespace kalve137::geometry {

TopoDS_Shape makeBox(double dx, double dy, double dz)
{
    return BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
}

TopoDS_Shape makeCylinder(const gp_Pnt& baseCenter, double radius, double height)
{
    const gp_Ax2 axis(baseCenter, gp_Dir(0.0, 0.0, 1.0));
    return BRepPrimAPI_MakeCylinder(axis, radius, height).Shape();
}

TopoDS_Shape cut(const TopoDS_Shape& base, const TopoDS_Shape& tool, BRepTools_History* history)
{
    BRepAlgoAPI_Cut op(base, tool);
    if (!op.IsDone()) {
        throw std::runtime_error("Boolean cut failed");
    }
    record(history, op.History());
    return op.Shape();
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

double area(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);
    return props.Mass();
}

gp_Pnt areaCentroid(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);
    return props.CentreOfMass();
}

double length(const TopoDS_Shape& shape)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    double total = 0.0;
    for (int i = 1; i <= edges.Extent(); ++i) {
        GProp_GProps props;
        BRepGProp::LinearProperties(edges(i), props);
        total += props.Mass();
    }
    return total;
}

} // namespace kalve137::geometry
