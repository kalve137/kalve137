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
#include <OSD_Parallel.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>

#include <cstddef>
#include <numeric>
#include <stdexcept>

namespace kalve137::geometry {

namespace {

template <typename Measure>
double sumOver(const std::vector<TopoDS_Shape>& shapes, Measure measure)
{
    std::vector<double> values(shapes.size(), 0.0);
    OSD_Parallel::For(0, static_cast<int>(shapes.size()), [&](int i) {
        values[static_cast<std::size_t>(i)] = measure(shapes[static_cast<std::size_t>(i)]);
    });
    return std::accumulate(values.begin(), values.end(), 0.0);
}

} // namespace

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

double totalArea(const std::vector<TopoDS_Shape>& shapes)
{
    return sumOver(shapes, [](const TopoDS_Shape& shape) { return area(shape); });
}

double totalLength(const std::vector<TopoDS_Shape>& shapes)
{
    return sumOver(shapes, [](const TopoDS_Shape& shape) { return length(shape); });
}

} // namespace kalve137::geometry
