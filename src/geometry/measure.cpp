// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/measure.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::geometry {

namespace {

struct Direction {
    gp_Dir dir;
    bool normal = false;
};

std::optional<Direction> directionOf(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() == TopAbs_EDGE) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        if (curve.GetType() == GeomAbs_Line) {
            return Direction{curve.Line().Direction(), false};
        }
    } else if (shape.ShapeType() == TopAbs_FACE) {
        const BRepAdaptor_Surface surface(TopoDS::Face(shape));
        if (surface.GetType() == GeomAbs_Plane) {
            return Direction{surface.Plane().Axis().Direction(), true};
        }
    }
    return std::nullopt;
}

double degrees(double radians)
{
    return radians * 180.0 / std::numbers::pi;
}

} // namespace

Measurement measure(const TopoDS_Shape& shape)
{
    Measurement m;
    if (shape.IsNull()) {
        return m;
    }
    GProp_GProps props;
    switch (shape.ShapeType()) {
    case TopAbs_VERTEX:
        m.position = BRep_Tool::Pnt(TopoDS::Vertex(shape));
        break;
    case TopAbs_EDGE: {
        BRepGProp::LinearProperties(shape, props);
        m.length = props.Mass();
        const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        if (curve.GetType() == GeomAbs_Circle) {
            m.radius = curve.Circle().Radius();
        }
        break;
    }
    case TopAbs_FACE: {
        BRepGProp::SurfaceProperties(shape, props);
        m.area = props.Mass();
        const BRepAdaptor_Surface surface(TopoDS::Face(shape));
        if (surface.GetType() == GeomAbs_Cylinder) {
            m.radius = surface.Cylinder().Radius();
        } else if (surface.GetType() == GeomAbs_Sphere) {
            m.radius = surface.Sphere().Radius();
        }
        break;
    }
    default:
        break;
    }
    return m;
}

Measurement measure(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    Measurement m;
    if (a.IsNull() || b.IsNull()) {
        return m;
    }
    BRepExtrema_DistShapeShape extrema(a, b);
    if (extrema.IsDone() && extrema.NbSolution() > 0) {
        m.distance = extrema.Value();
        m.from = extrema.PointOnShape1(1);
        m.to = extrema.PointOnShape2(1);
    }
    const std::optional<Direction> da = directionOf(a);
    const std::optional<Direction> db = directionOf(b);
    if (da && db) {
        const double between = std::acos(std::clamp(std::abs(da->dir.Dot(db->dir)), 0.0, 1.0));
        m.angle = degrees(da->normal == db->normal ? between : std::numbers::pi / 2.0 - between);
    }
    return m;
}

BodyProperties properties(const TopoDS_Shape& shape)
{
    BodyProperties result;
    GProp_GProps volumeProps;
    BRepGProp::VolumeProperties(shape, volumeProps);
    result.volume = volumeProps.Mass();
    result.centerOfMass = volumeProps.CentreOfMass();
    GProp_GProps surfaceProps;
    BRepGProp::SurfaceProperties(shape, surfaceProps);
    result.area = surfaceProps.Mass();
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (!box.IsVoid()) {
        result.boxMin = box.CornerMin();
        result.boxMax = box.CornerMax();
    }
    return result;
}

} // namespace kalve137::geometry
