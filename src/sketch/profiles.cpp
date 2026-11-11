// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/profiles.h"

#include <BOPAlgo_Tools.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <cmath>
#include <numbers>

namespace kalve137::sketch {

namespace {

constexpr double kMinLength = 1e-7;

}

gp_Pnt toWorld(const gp_Ax3& plane, Vec2 p)
{
    const gp_XYZ o = plane.Location().XYZ();
    return gp_Pnt(o + plane.XDirection().XYZ() * p.x + plane.YDirection().XYZ() * p.y);
}

Vec2 toSketch(const gp_Ax3& plane, const gp_Pnt& p)
{
    const gp_XYZ v = p.XYZ() - plane.Location().XYZ();
    return {v.Dot(plane.XDirection().XYZ()), v.Dot(plane.YDirection().XYZ())};
}

std::optional<TopoDS_Edge> makeEdge(const Sketch& sketch, Id id, const gp_Ax3& plane)
{
    const Curve* curve = sketch.curve(id);
    if (curve == nullptr) {
        return std::nullopt;
    }
    const auto pos = [&](std::size_t i) { return sketch.point(curve->points[i])->pos; };

    switch (curve->kind) {
    case CurveKind::Line: {
        if (distance(pos(0), pos(1)) < kMinLength) {
            return std::nullopt;
        }
        BRepBuilderAPI_MakeEdge edge(toWorld(plane, pos(0)), toWorld(plane, pos(1)));
        return edge.IsDone() ? std::optional(edge.Edge()) : std::nullopt;
    }
    case CurveKind::Circle: {
        if (curve->radius < kMinLength) {
            return std::nullopt;
        }
        const gp_Circ circ(gp_Ax2(toWorld(plane, pos(0)), plane.Direction(), plane.XDirection()), curve->radius);
        BRepBuilderAPI_MakeEdge edge(circ);
        return edge.IsDone() ? std::optional(edge.Edge()) : std::nullopt;
    }
    case CurveKind::Arc: {
        if (curve->radius < kMinLength) {
            return std::nullopt;
        }
        const Vec2 c = pos(0);
        const Vec2 s = pos(1);
        const Vec2 e = pos(2);
        const double start = std::atan2(s.y - c.y, s.x - c.x);
        double end = std::atan2(e.y - c.y, e.x - c.x);
        if (end <= start) {
            end += 2.0 * std::numbers::pi;
        }
        const gp_Circ circ(gp_Ax2(toWorld(plane, c), plane.Direction(), plane.XDirection()), curve->radius);
        BRepBuilderAPI_MakeEdge edge(circ, start, end);
        return edge.IsDone() ? std::optional(edge.Edge()) : std::nullopt;
    }
    }
    return std::nullopt;
}

std::vector<TopoDS_Face> findProfiles(const Sketch& sketch, const gp_Ax3& plane)
{
    BRep_Builder builder;
    TopoDS_Compound edges;
    builder.MakeCompound(edges);
    int count = 0;
    for (const auto& [id, curve] : sketch.curves()) {
        if (curve.construction) {
            continue;
        }
        if (auto edge = makeEdge(sketch, id, plane)) {
            builder.Add(edges, *edge);
            ++count;
        }
    }
    if (count == 0) {
        return {};
    }

    TopoDS_Shape wires;
    if (BOPAlgo_Tools::EdgesToWires(edges, wires) != 0) {
        return {};
    }
    TopoDS_Shape faces;
    if (!BOPAlgo_Tools::WiresToFaces(wires, faces)) {
        return {};
    }

    std::vector<TopoDS_Face> result;
    for (TopExp_Explorer it(faces, TopAbs_FACE); it.More(); it.Next()) {
        result.push_back(TopoDS::Face(it.Current()));
    }
    return result;
}

std::optional<Vec2> interiorPoint(const TopoDS_Face& face, const gp_Ax3& plane)
{
    BRepMesh_IncrementalMesh mesh(face, 0.1, true);
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0) {
        return std::nullopt;
    }

    double bestArea = -1.0;
    gp_Pnt best;
    for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
        int n1 = 0;
        int n2 = 0;
        int n3 = 0;
        triangulation->Triangle(i).Get(n1, n2, n3);
        const gp_Pnt a = triangulation->Node(n1).Transformed(location.Transformation());
        const gp_Pnt b = triangulation->Node(n2).Transformed(location.Transformation());
        const gp_Pnt c = triangulation->Node(n3).Transformed(location.Transformation());
        const double area = gp_Vec(a, b).Crossed(gp_Vec(a, c)).Magnitude();
        if (area > bestArea) {
            bestArea = area;
            best = gp_Pnt((a.XYZ() + b.XYZ() + c.XYZ()) / 3.0);
        }
    }
    return toSketch(plane, best);
}

std::optional<std::size_t> profileAt(const std::vector<TopoDS_Face>& profiles, const gp_Ax3& plane, Vec2 p)
{
    const gp_Pnt world = toWorld(plane, p);
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        BRepClass_FaceClassifier classifier(profiles[i], world, 1e-6);
        if (classifier.State() == TopAbs_IN) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace kalve137::sketch
