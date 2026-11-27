// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/body_mesh.h"
#include "geometry/exchange.h"
#include "geometry/topology.h"

#include <gtest/gtest.h>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <NCollection_IndexedMap.hxx>
#include <OSD_Parallel.hxx>
#include <Prs3d_Drawer.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::geometry {
namespace {

constexpr double kFineDeflection = 0.001;
constexpr double kFineAngle = 0.5;

std::string environment(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    _dupenv_s(&value, &length, name);
    std::string text = value != nullptr ? value : "";
    std::free(value);
    return text;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

struct Counts {
    int faces = 0;
    int edges = 0;
};

int countVisible(const std::vector<TopoDS_Shape>& shapes, TopAbs_ShapeEnum kind, const ViewBlockers& blockers,
                 const gp_Dir& towardViewer)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> items;
    for (const TopoDS_Shape& shape : shapes) {
        TopExp::MapShapes(shape, kind, items);
    }
    std::atomic<int> visible = 0;
    OSD_Parallel::For(1, items.Extent() + 1, [&](int i) {
        const std::vector<gp_Pnt> points = samplePoints(items(i));
        if (std::ranges::any_of(points, [&](const gp_Pnt& point) { return !blockers.blocks(point, towardViewer); })) {
            ++visible;
        }
    });
    return visible;
}

Counts countVisible(const std::vector<TopoDS_Shape>& shapes, const gp_Dir& towardViewer)
{
    std::vector<snapshot::Mesh> meshes;
    meshes.reserve(shapes.size());
    for (const TopoDS_Shape& shape : shapes) {
        meshes.push_back(meshBody(shape));
    }
    std::vector<const snapshot::Mesh*> pointers;
    pointers.reserve(meshes.size());
    for (const snapshot::Mesh& mesh : meshes) {
        pointers.push_back(&mesh);
    }
    const ViewBlockers blockers(pointers);
    return {countVisible(shapes, TopAbs_FACE, blockers, towardViewer),
            countVisible(shapes, TopAbs_EDGE, blockers, towardViewer)};
}

std::vector<TopoDS_Shape> copies(const std::vector<StepPart>& parts)
{
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(parts.size());
    for (const StepPart& part : parts) {
        shapes.push_back(BRepBuilderAPI_Copy(part.shape, true, false).Shape());
    }
    return shapes;
}

TEST(BoxSelectionCompare, DISABLED_DisplayMeshKeepsTheVisibleCounts)
{
    const std::string list = environment("KALVE137_BOX_COMPARE_STEPS");
    if (list.empty()) {
        GTEST_SKIP() << "Set KALVE137_BOX_COMPARE_STEPS to STEP files separated by ';'";
    }
    const std::vector<std::pair<const char*, gp_Dir>> views = {
        {"front", gp_Dir(0.0, -1.0, 0.0)}, {"top", gp_Dir(0.0, 0.0, 1.0)}, {"iso", gp_Dir(1.0, -1.0, 1.0)}};
    std::istringstream files(list);
    for (std::string file; std::getline(files, file, ';');) {
        const std::vector<StepPart> parts = readStepParts(file);
        const std::vector<TopoDS_Shape> drawn = copies(parts);
        const Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
        for (const TopoDS_Shape& shape : drawn) {
            StdPrs_ToolTriangulatedShape::Tessellate(shape, drawer);
        }
        const std::vector<TopoDS_Shape> fine = copies(parts);
        for (const TopoDS_Shape& shape : fine) {
            const BRepMesh_IncrementalMesh mesher(shape, kFineDeflection, true, kFineAngle);
        }
        const std::vector<TopoDS_Shape> display = copies(parts);
        for (const auto& [name, towardViewer] : views) {
            const Counts before = countVisible(drawn, towardViewer);
            const Counts fallback = countVisible(fine, towardViewer);
            const Counts after = countVisible(display, towardViewer);
            std::cout << std::filesystem::path(file).filename().string() << " " << name << ": faces " << before.faces
                      << " -> " << after.faces << " (fine " << fallback.faces << "), edges " << before.edges << " -> "
                      << after.edges << " (fine " << fallback.edges << ")\n";
            EXPECT_EQ(after.faces, before.faces) << file << " " << name;
            EXPECT_EQ(after.edges, before.edges) << file << " " << name;
        }
    }
}

} // namespace
} // namespace kalve137::geometry
