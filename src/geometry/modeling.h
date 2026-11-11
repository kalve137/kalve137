// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <optional>
#include <vector>

class BRepTools_History;

namespace kalve137::geometry {

TopoDS_Shape extrude(const std::vector<TopoDS_Face>& faces, const gp_Vec& from, const gp_Vec& to,
                     double taperRadians = 0.0);
TopoDS_Shape revolve(const std::vector<TopoDS_Face>& faces, const gp_Ax1& axis, double angleRadians);

TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b, BRepTools_History* history = nullptr);
TopoDS_Shape common(const TopoDS_Shape& a, const TopoDS_Shape& b, BRepTools_History* history = nullptr);
TopoDS_Shape pattern(const TopoDS_Shape& shape, const std::vector<gp_Trsf>& placements,
                     BRepTools_History* history = nullptr);
bool overlaps(const TopoDS_Shape& a, const TopoDS_Shape& b);
bool isEmpty(const TopoDS_Shape& shape);

TopoDS_Shape fillet(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, double radius,
                    BRepTools_History* history = nullptr);
TopoDS_Shape fillet(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, const std::vector<double>& radii,
                    BRepTools_History* history = nullptr);
TopoDS_Shape chamfer(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, double distance,
                     BRepTools_History* history = nullptr);
TopoDS_Shape shell(const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& openFaces, double thickness, bool inside,
                   BRepTools_History* history = nullptr);

TopoDS_Shape offsetFaces(const TopoDS_Shape& shape, const std::vector<TopoDS_Face>& faces, double distance,
                         BRepTools_History* history = nullptr);

enum class HoleHead { None, Counterbore, Countersink };

struct HoleParams {
    HoleHead head = HoleHead::None;
    double diameter = 5.0;
    double depth = 10.0;
    double headDiameter = 9.0;
    double headDepth = 3.0;
    double countersinkAngleDegrees = 90.0;
    double tipAngleDegrees = 118.0;
};

TopoDS_Shape holeTool(const gp_Ax1& axis, const HoleParams& params);

gp_Pnt edgeMidpoint(const TopoDS_Edge& edge);
std::optional<TopoDS_Edge> findEdge(const TopoDS_Shape& shape, const gp_Pnt& pointOnEdge, double tolerance);

gp_Pnt facePoint(const TopoDS_Face& face);
std::optional<gp_Ax3> planarFrame(const TopoDS_Face& face);
std::optional<TopoDS_Face> findFace(const TopoDS_Shape& shape, const gp_Pnt& pointOnFace, double tolerance);

} // namespace kalve137::geometry
