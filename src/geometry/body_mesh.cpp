// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/body_mesh.h"

#include "geometry/meshing.h"

#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRep_Tool.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Trsf.hxx>

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace kalve137::geometry {

namespace {

snapshot::Point point(const gp_XYZ& xyz)
{
    return {xyz.X(), xyz.Y(), xyz.Z()};
}

void addFaces(const TopTools_IndexedMapOfShape& faces, snapshot::Mesh& mesh,
              std::unordered_map<int, std::uint32_t>& baseOf)
{
    for (int index = 1; index <= faces.Extent(); ++index) {
        const TopoDS_Face& face = TopoDS::Face(faces(index));
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        const auto id = mesh.faceCount++;
        if (triangulation.IsNull()) {
            mesh.faceDeflections.push_back(0.0);
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
        for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
            int a = 0;
            int b = 0;
            int c = 0;
            triangulation->Triangle(i).Get(a, b, c);
            if (reversed) {
                std::swap(b, c);
            }
            mesh.triangles.push_back({base + static_cast<std::uint32_t>(a - 1),
                                      base + static_cast<std::uint32_t>(b - 1),
                                      base + static_cast<std::uint32_t>(c - 1)});
            mesh.faces.push_back(id);
        }
    }
}

void addEdges(const TopoDS_Shape& shape, const TopTools_IndexedMapOfShape& faces,
              const std::unordered_map<int, std::uint32_t>& baseOf, snapshot::Mesh& mesh)
{
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    mesh.edgeCount = static_cast<std::uint32_t>(edges.Extent());
    TopTools_IndexedDataMapOfShapeListOfShape facesOf;
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
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    mesh.pointCount = static_cast<std::uint32_t>(vertices.Extent());
    mesh.points.reserve(static_cast<std::size_t>(vertices.Extent()));
    for (int index = 1; index <= vertices.Extent(); ++index) {
        mesh.points.push_back({.id = static_cast<std::uint32_t>(index - 1),
                               .point = point(BRep_Tool::Pnt(TopoDS::Vertex(vertices(index))).XYZ())});
    }
}

} // namespace

snapshot::Mesh meshBody(const TopoDS_Shape& shape)
{
    snapshot::Mesh mesh;
    if (shape.IsNull()) {
        return mesh;
    }
    ensureMesh(shape);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    std::unordered_map<int, std::uint32_t> baseOf;
    addFaces(faces, mesh, baseOf);
    addEdges(shape, faces, baseOf, mesh);
    addPoints(shape, mesh);
    return mesh;
}

} // namespace kalve137::geometry
