// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/body_mesh.h"

#include "geometry/meshing.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Trsf.hxx>

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kalve137::geometry {

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;

snapshot::Point point(const gp_XYZ& xyz)
{
    return {xyz.X(), xyz.Y(), xyz.Z()};
}

bool addFaces(const ShapeMap& faces, snapshot::Mesh& mesh, std::unordered_map<int, std::uint32_t>& baseOf)
{
    bool complete = true;
    for (int index = 1; index <= faces.Extent(); ++index) {
        const TopoDS_Face& face = TopoDS::Face(faces(index));
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        const auto id = mesh.faceCount++;
        if (triangulation.IsNull()) {
            mesh.faceDeflections.push_back(0.0);
            complete = false;
            continue;
        }
        mesh.faceDeflections.push_back(triangulation->Deflection());
        if (!triangulation->HasNormals()) {
            BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
        }
        const gp_Trsf placement = location.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        baseOf[index] = base;
        for (int i = 1; i <= triangulation->NbNodes(); ++i) {
            mesh.vertices.push_back(point(triangulation->Node(i).Transformed(placement).XYZ()));
            gp_Dir normal = triangulation->Normal(i).Transformed(placement);
            if (reversed) {
                normal.Reverse();
            }
            mesh.normals.push_back(point(normal.XYZ()));
        }
        const bool flipped = reversed != placement.IsNegative();
        for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
            int a = 0;
            int b = 0;
            int c = 0;
            triangulation->Triangle(i).Get(a, b, c);
            if (flipped) {
                std::swap(b, c);
            }
            mesh.triangles.push_back({base + static_cast<std::uint32_t>(a - 1),
                                      base + static_cast<std::uint32_t>(b - 1),
                                      base + static_cast<std::uint32_t>(c - 1)});
            mesh.faces.push_back(id);
        }
    }
    return complete;
}

void addEdges(const TopoDS_Shape& shape, const ShapeMap& faces, const std::unordered_map<int, std::uint32_t>& baseOf,
              snapshot::Mesh& mesh)
{
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    mesh.edgeCount = static_cast<std::uint32_t>(edges.Extent());
    AncestorMap facesOf;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
    for (int i = 1; i <= facesOf.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(facesOf.FindKey(i));
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        const auto id = static_cast<std::uint32_t>(edges.FindIndex(edge) - 1);
        for (const TopoDS_Shape& face : facesOf(i)) {
            const auto found = baseOf.find(faces.FindIndex(face));
            if (found == baseOf.end()) {
                continue;
            }
            TopLoc_Location location;
            const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(TopoDS::Face(face), location);
            const Handle(Poly_PolygonOnTriangulation) polygon =
                BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
            if (polygon.IsNull()) {
                continue;
            }
            snapshot::Edge line{.id = id, .points = {}};
            line.points.reserve(static_cast<std::size_t>(polygon->NbNodes()));
            for (int node = 1; node <= polygon->NbNodes(); ++node) {
                line.points.push_back(
                    mesh.vertices[found->second + static_cast<std::uint32_t>(polygon->Node(node) - 1)]);
            }
            mesh.edges.push_back(std::move(line));
            break;
        }
    }
}

void addPoints(const TopoDS_Shape& shape, snapshot::Mesh& mesh)
{
    ShapeMap vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    mesh.pointCount = static_cast<std::uint32_t>(vertices.Extent());
    mesh.points.reserve(static_cast<std::size_t>(vertices.Extent()));
    for (int index = 1; index <= vertices.Extent(); ++index) {
        mesh.points.push_back({.id = static_cast<std::uint32_t>(index - 1),
                               .point = point(BRep_Tool::Pnt(TopoDS::Vertex(vertices(index))).XYZ())});
    }
}

bool isClosed(const TopoDS_Shape& shape)
{
    if (TopExp_Explorer loose(shape, TopAbs_FACE, TopAbs_SOLID); loose.More()) {
        return false;
    }
    bool shells = false;
    for (TopExp_Explorer shell(shape, TopAbs_SHELL); shell.More(); shell.Next()) {
        if (!BRep_Tool::IsClosed(shell.Current())) {
            return false;
        }
        shells = true;
    }
    return shells;
}

} // namespace

snapshot::Mesh meshBody(const TopoDS_Shape& shape)
{
    snapshot::Mesh mesh;
    if (shape.IsNull()) {
        return mesh;
    }
    ensureMesh(shape);
    ShapeMap faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    std::unordered_map<int, std::uint32_t> baseOf;
    const bool complete = addFaces(faces, mesh, baseOf);
    addEdges(shape, faces, baseOf, mesh);
    addPoints(shape, mesh);
    snapshot::setBounds(mesh);
    mesh.closed = complete && isClosed(shape);
    return mesh;
}

std::vector<snapshot::Polyline> edgeLines(const TopoDS_Shape& shape)
{
    std::vector<snapshot::Polyline> lines;
    const double deflection = shape.IsNull() ? 0.0 : displayDeflection(shape);
    if (deflection <= 0.0) {
        return lines;
    }
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int index = 1; index <= edges.Extent(); ++index) {
        const TopoDS_Edge& edge = TopoDS::Edge(edges(index));
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        const BRepAdaptor_Curve curve(edge);
        const GCPnts_TangentialDeflection points(curve, kDisplayDeviationAngle, deflection);
        snapshot::Polyline line;
        line.reserve(static_cast<std::size_t>(points.NbPoints()));
        for (int i = 1; i <= points.NbPoints(); ++i) {
            line.push_back(point(points.Value(i).XYZ()));
        }
        if (line.size() > 1) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

} // namespace kalve137::geometry
