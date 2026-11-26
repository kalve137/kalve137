// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "geometry/body_mesh.h"
#include "geometry/meshing.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>

namespace {

using kalve137::geometry::displayDeflection;
using kalve137::geometry::meshBody;
using kalve137::snapshot::Mesh;
using kalve137::snapshot::Point;

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

Point minus(const Point& a, const Point& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Point cross(const Point& a, const Point& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

double dot(const Point& a, const Point& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

TEST(BodyMesh, BoxHasSixFacesTwelveEdgesAndOutwardTriangles)
{
    const Mesh mesh = meshBody(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
    EXPECT_EQ(mesh.faceCount, 6U);
    EXPECT_EQ(mesh.faceDeflections.size(), 6U);
    EXPECT_EQ(mesh.triangles.size(), 12U);
    EXPECT_EQ(mesh.faces.size(), mesh.triangles.size());
    EXPECT_EQ(mesh.normals.size(), mesh.vertices.size());
    EXPECT_EQ(mesh.edges.size(), 12U);
    EXPECT_EQ(mesh.edgeCount, 12U);
    EXPECT_EQ(mesh.pointCount, 8U);
    EXPECT_EQ(std::set(mesh.faces.begin(), mesh.faces.end()).size(), 6U);

    const Point center{5.0, 10.0, 15.0};
    for (const auto& triangle : mesh.triangles) {
        const Point& a = mesh.vertices[triangle[0]];
        const Point normal = cross(minus(mesh.vertices[triangle[1]], a), minus(mesh.vertices[triangle[2]], a));
        EXPECT_GT(dot(normal, minus(a, center)), 0.0);
        EXPECT_GT(dot(normal, mesh.normals[triangle[0]]), 0.0);
    }
}

TEST(BodyMesh, CylinderIsFineEnoughToLookRound)
{
    const Mesh mesh = meshBody(BRepPrimAPI_MakeCylinder(gp_Ax2(), 10.0, 5.0).Shape());
    EXPECT_EQ(mesh.faceCount, 3U);
    EXPECT_GE(mesh.triangles.size(), 36U);
    EXPECT_EQ(mesh.edges.size(), 3U);
    for (const Point& point : mesh.vertices) {
        if (point[2] > 1e-9 && point[2] < 5.0 - 1e-9) {
            EXPECT_NEAR(std::hypot(point[0], point[1]), 10.0, 1e-6);
        }
    }
}

double distance(const Point& a, const gp_Pnt& b)
{
    return b.Distance(gp_Pnt(a[0], a[1], a[2]));
}

void expectIdsFollowTheShape(const TopoDS_Shape& shape)
{
    const Mesh mesh = meshBody(shape);
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    ASSERT_EQ(mesh.edgeCount, static_cast<std::uint32_t>(edges.Extent()));
    std::set<std::uint32_t> seen;
    for (const auto& edge : mesh.edges) {
        ASSERT_LT(edge.id, mesh.edgeCount);
        EXPECT_TRUE(seen.insert(edge.id).second) << edge.id;
        const TopoDS_Edge& shapeEdge = TopoDS::Edge(edges(static_cast<int>(edge.id) + 1));
        EXPECT_FALSE(BRep_Tool::Degenerated(shapeEdge)) << edge.id;
        TopoDS_Vertex first;
        TopoDS_Vertex last;
        TopExp::Vertices(shapeEdge, first, last);
        for (const Point& end : {edge.points.front(), edge.points.back()}) {
            EXPECT_LT(std::min(distance(end, BRep_Tool::Pnt(first)), distance(end, BRep_Tool::Pnt(last))), 1e-6)
                << edge.id;
        }
    }
    for (int index = 1; index <= edges.Extent(); ++index) {
        if (BRep_Tool::Degenerated(TopoDS::Edge(edges(index)))) {
            EXPECT_FALSE(seen.contains(static_cast<std::uint32_t>(index - 1))) << index;
        }
    }

    ShapeMap vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    ASSERT_EQ(mesh.pointCount, static_cast<std::uint32_t>(vertices.Extent()));
    ASSERT_EQ(mesh.points.size(), mesh.pointCount);
    for (const auto& vertex : mesh.points) {
        EXPECT_LT(distance(vertex.point, BRep_Tool::Pnt(TopoDS::Vertex(vertices(static_cast<int>(vertex.id) + 1)))),
                  1e-9);
    }
}

int degenerateEdges(const TopoDS_Shape& shape)
{
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    int count = 0;
    for (int index = 1; index <= edges.Extent(); ++index) {
        count += BRep_Tool::Degenerated(TopoDS::Edge(edges(index))) ? 1 : 0;
    }
    return count;
}

TEST(BodyMesh, ConeEdgeIdsSkipTheApex)
{
    const TopoDS_Shape cone = BRepPrimAPI_MakeCone(10.0, 0.0, 20.0).Shape();
    ASSERT_GT(degenerateEdges(cone), 0);
    expectIdsFollowTheShape(cone);
}

TEST(BodyMesh, SphereEdgeIdsSkipThePoles)
{
    const TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(10.0).Shape();
    ASSERT_GT(degenerateEdges(sphere), 0);
    expectIdsFollowTheShape(sphere);
}

TEST(BodyMesh, BoxIdsFollowTheShape)
{
    expectIdsFollowTheShape(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
}

TEST(BodyMesh, MeshesAShapeThatHasNoTriangulation)
{
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(), 10.0, 5.0).Shape();
    ASSERT_FALSE(BRepTools::Triangulation(cylinder, displayDeflection(cylinder)));
    EXPECT_FALSE(meshBody(cylinder).triangles.empty());
    EXPECT_TRUE(BRepTools::Triangulation(cylinder, displayDeflection(cylinder)));
}

TEST(BodyMesh, KeepsAFinerStoredTriangulation)
{
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(), 10.0, 5.0).Shape();
    const std::size_t display = meshBody(BRepPrimAPI_MakeCylinder(gp_Ax2(), 10.0, 5.0).Shape()).triangles.size();
    const BRepMesh_IncrementalMesh finer(cylinder, displayDeflection(cylinder) / 10.0, false, 0.1);
    EXPECT_GT(meshBody(cylinder).triangles.size(), display);
}

void expectOutward(const Mesh& mesh)
{
    for (const auto& triangle : mesh.triangles) {
        const Point& a = mesh.vertices[triangle[0]];
        const Point normal = cross(minus(mesh.vertices[triangle[1]], a), minus(mesh.vertices[triangle[2]], a));
        EXPECT_GT(dot(normal, mesh.normals[triangle[0]]), 0.0);
    }
}

TopoDS_Shape pocketedBox()
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 40.0, 20.0).Shape();
    const TopoDS_Shape pocket = BRepPrimAPI_MakeBox(gp_Pnt(10.0, 10.0, 8.0), 20.0, 20.0, 20.0).Shape();
    return BRepAlgoAPI_Cut(box, pocket).Shape();
}

TEST(BodyMesh, OnlySolidsWithClosedShellsAreClosed)
{
    EXPECT_TRUE(meshBody(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()).closed);
    EXPECT_TRUE(meshBody(BRepPrimAPI_MakeSphere(10.0).Shape()).closed);
    EXPECT_TRUE(meshBody(pocketedBox()).closed);

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
    ShapeMap faces;
    TopExp::MapShapes(box, TopAbs_FACE, faces);
    EXPECT_FALSE(meshBody(faces(1)).closed) << "loose face";

    BRep_Builder builder;
    TopoDS_Shell shell;
    builder.MakeShell(shell);
    for (int index = 1; index < faces.Extent(); ++index) {
        builder.Add(shell, faces(index));
    }
    TopoDS_Solid solid;
    builder.MakeSolid(solid);
    builder.Add(solid, shell);
    EXPECT_FALSE(meshBody(solid).closed) << "box with its last face missing";

    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(50.0, 0.0, 0.0), 1.0, 1.0, 1.0).BottomFace());
    EXPECT_FALSE(meshBody(compound).closed) << "solid next to a loose face";
}

TEST(BodyMesh, AMirroredPlacementKeepsTrianglesOutward)
{
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)));
    ASSERT_TRUE(mirror.IsNegative());
    const TopoDS_Shape mirrored = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape().Moved(TopLoc_Location(mirror));
    const Mesh mesh = meshBody(mirrored);
    ASSERT_EQ(mesh.triangles.size(), 12U);
    expectOutward(mesh);
    const Point center{-5.0, 10.0, 15.0};
    for (const auto& triangle : mesh.triangles) {
        const Point& a = mesh.vertices[triangle[0]];
        const Point normal = cross(minus(mesh.vertices[triangle[1]], a), minus(mesh.vertices[triangle[2]], a));
        EXPECT_GT(dot(normal, minus(a, center)), 0.0);
    }
}

TEST(BodyMesh, CullingAPocketedBoxDrawsTheSame)
{
    const TopoDS_Shape shape = pocketedBox();
    auto mesh = std::make_shared<Mesh>(meshBody(shape));
    expectOutward(*mesh);
    const auto body = std::make_shared<const kalve137::snapshot::Body>(
        kalve137::snapshot::Body{.feature = 1, .part = 0, .mesh = mesh, .appearance = {}});
    const kalve137::snapshot::Scene scene{.bodies = {body}, .origin = {20.0, 20.0, 10.0}, .hidden = {}};
    kalve137::camera::Camera camera;
    ASSERT_TRUE(camera.setEyeAndCenter({70.0, -40.0, 90.0}, {20.0, 20.0, 10.0}));
    ASSERT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    ASSERT_TRUE(camera.setScale(70.0));
    kalve137::render::ViewportStyle style{.background = {0.1F, 0.2F, 0.3F},
                                          .body = {0.6F, 0.65F, 0.7F},
                                          .visualStyle = kalve137::render::VisualStyle::Shaded,
                                          .pixelRatio = 1.0,
                                          .cullClosed = true};
    const kalve137::render::Frame frame =
        kalve137::render::makeFrame(camera, {240, 200, 1.0}, scene.origin, style.background);
    kalve137::render::CpuBackend backend(4);
    std::array<kalve137::render::Image, 2> images;
    for (const bool cull : {true, false}) {
        style.cullClosed = cull;
        kalve137::render::ChunkSet chunks;
        chunks.update(scene, style);
        const kalve137::render::DrawList list = chunks.draw(frame);
        ASSERT_EQ(list.chunks.size(), 1U);
        EXPECT_TRUE(list.chunks[0].closed);
        backend.draw(list, 2, images[cull ? 0 : 1]);
    }
    const std::uint32_t background = images[1].at(0, 0);
    EXPECT_GT(std::ranges::count_if(images[1].pixels, [&](std::uint32_t pixel) { return pixel != background; }),
              240 * 200 / 5);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < images[0].pixels.size(); ++i) {
        changed += images[0].pixels[i] != images[1].pixels[i] ? 1 : 0;
    }
    EXPECT_EQ(changed, 0U);
}

TEST(BodyMesh, RemeshesACoarserStoredTriangulation)
{
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(), 10.0, 5.0).Shape();
    const BRepMesh_IncrementalMesh coarser(cylinder, 5.0, false, 1.5);
    ASSERT_FALSE(BRepTools::Triangulation(cylinder, displayDeflection(cylinder)));
    const Mesh mesh = meshBody(cylinder);
    for (const double deflection : mesh.faceDeflections) {
        EXPECT_LE(deflection, displayDeflection(cylinder));
    }
}

} // namespace
