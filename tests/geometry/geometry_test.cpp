// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/exchange.h"
#include "geometry/face_colors.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"
#include "geometry/topology.h"

#include <gtest/gtest.h>

#include <BRepTools_History.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kalve137::geometry;

TEST(Geometry, BoxVolume)
{
    EXPECT_NEAR(volume(makeBox(100.0, 50.0, 90.0)), 450000.0, 1e-6);
}

TEST(Geometry, BoxAreaAndEdgeLength)
{
    const TopoDS_Shape box = makeBox(10.0, 20.0, 30.0);
    EXPECT_NEAR(area(box), 2.0 * (10.0 * 20.0 + 10.0 * 30.0 + 20.0 * 30.0), 1e-6);
    EXPECT_NEAR(length(box), 4.0 * (10.0 + 20.0 + 30.0), 1e-6);
}

TEST(Geometry, CutCylinderThroughBox)
{
    const TopoDS_Shape box = makeBox(100.0, 50.0, 90.0);
    const TopoDS_Shape hole = makeCylinder(gp_Pnt(50.0, 25.0, 0.0), 10.0, 90.0);

    const double expected = 450000.0 - std::numbers::pi * 10.0 * 10.0 * 90.0;
    EXPECT_NEAR(volume(cut(box, hole)), expected, 1e-3);
}

TEST(Modeling, ShellWithoutOpenFacesLeavesClosedCavity)
{
    const TopoDS_Shape hollow = shell(makeBox(20.0, 40.0, 10.0), {}, 2.0, true);
    EXPECT_NEAR(volume(hollow), 8000.0 - 16.0 * 36.0 * 6.0, 1e-3);
}

TEST(Modeling, ShellRejectsWallsThickerThanBody)
{
    const TopoDS_Shape box = makeBox(20.0, 40.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(10.0, 20.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    EXPECT_THROW(shell(box, {*top}, 15.0, true), std::runtime_error);
}

TEST(Modeling, ShellThickerThanRoundsLeavesSharpInnerCorners)
{
    const TopoDS_Shape box = makeBox(20.0, 40.0, 10.0);
    std::vector<TopoDS_Edge> edges;
    for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
        if (edgeMidpoint(edge).Z() < 9.0) {
            edges.push_back(edge);
        }
    }
    const TopoDS_Shape rounded = fillet(box, edges, 1.0);
    const std::optional<TopoDS_Face> top = findFace(rounded, gp_Pnt(10.0, 20.0, 10.0), 1e-3);
    ASSERT_TRUE(top);

    EXPECT_NEAR(volume(shell(rounded, {*top}, 2.0, true)), volume(rounded) - 16.0 * 36.0 * 8.0, 1e-3);
    EXPECT_GT(volume(shell(rounded, {*top}, 0.5, true)), 0.0);
}

TEST(Modeling, ShellNamesTheRoundAnOpenFaceRunsInto)
{
    const TopoDS_Shape box = makeBox(60.0, 30.0, 10.0);
    std::vector<TopoDS_Edge> edges;
    for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
        if (edgeMidpoint(edge).Distance(gp_Pnt(30.0, 0.0, 10.0)) < 1e-6) {
            edges.push_back(edge);
        }
    }
    const TopoDS_Shape rounded = fillet(box, edges, 3.0);
    const std::optional<TopoDS_Face> top = findFace(rounded, gp_Pnt(30.0, 15.0, 10.0), 1e-3);
    ASSERT_TRUE(top);

    try {
        shell(rounded, {*top}, 2.0, true);
        SUCCEED();
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("runs smoothly into a round"), std::string::npos) << error.what();
    }
}

TEST(Modeling, FacePointAvoidsHoles)
{
    const TopoDS_Shape block = cut(makeBox(100.0, 50.0, 90.0), makeCylinder(gp_Pnt(50.0, 25.0, 0.0), 10.0, 90.0));
    const std::optional<TopoDS_Face> top = findFace(block, gp_Pnt(5.0, 5.0, 90.0), 1e-3);
    ASSERT_TRUE(top);
    const gp_Pnt point = facePoint(*top);
    EXPECT_NEAR(point.Z(), 90.0, 1e-6);
    EXPECT_GT(gp_Pnt(50.0, 25.0, 90.0).Distance(point), 10.0);
    const std::optional<TopoDS_Face> found = findFace(block, point, 1e-3);
    ASSERT_TRUE(found);
    EXPECT_TRUE(found->IsSame(*top));
}

TEST(Topology, TangentChainFollowsFilletedCorner)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Edge> corner = findEdge(box, gp_Pnt(10.0, 10.0, 5.0), 1e-3);
    ASSERT_TRUE(corner);
    const TopoDS_Shape rounded = fillet(box, {*corner}, 2.0);

    const std::optional<TopoDS_Edge> top = findEdge(rounded, gp_Pnt(4.0, 10.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    const std::vector<TopoDS_Edge> chain = tangentChain(rounded, *top);
    ASSERT_EQ(chain.size(), 3u);
    double total = 0.0;
    for (const TopoDS_Edge& edge : chain) {
        total += length(edge);
    }
    EXPECT_NEAR(total, 8.0 + 8.0 + std::numbers::pi, 1e-6);

    const std::optional<TopoDS_Edge> sharp = findEdge(box, gp_Pnt(5.0, 0.0, 0.0), 1e-3);
    ASSERT_TRUE(sharp);
    EXPECT_EQ(tangentChain(box, *sharp).size(), 1u);
}

TEST(Topology, BlendSourceMapsPreviewPicksToSelectedEdges)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Edge> corner = findEdge(box, gp_Pnt(10.0, 10.0, 5.0), 1e-3);
    const std::optional<TopoDS_Edge> bottom = findEdge(box, gp_Pnt(5.0, 0.0, 0.0), 1e-3);
    ASSERT_TRUE(corner && bottom);
    const std::vector<TopoDS_Edge> edges = {*corner, *bottom};
    const TopoDS_Shape rounded = fillet(box, edges, 2.0);
    const double inset = 2.0 - std::numbers::sqrt2;

    const std::optional<TopoDS_Face> cornerFace = findFace(rounded, gp_Pnt(10.0 - inset, 10.0 - inset, 5.0), 1e-3);
    ASSERT_TRUE(cornerFace);
    const std::optional<TopoDS_Edge> source = blendSource(box, edges, 2.0, Blend::Fillet, *cornerFace);
    ASSERT_TRUE(source);
    EXPECT_TRUE(source->IsSame(*corner));

    const std::optional<TopoDS_Edge> arc = findEdge(rounded, gp_Pnt(10.0 - inset, 10.0 - inset, 10.0), 1e-3);
    ASSERT_TRUE(arc);
    const std::optional<TopoDS_Edge> fromArc = blendSource(box, edges, 2.0, Blend::Fillet, *arc);
    ASSERT_TRUE(fromArc);
    EXPECT_TRUE(fromArc->IsSame(*corner));

    const std::optional<TopoDS_Face> bottomFace = findFace(rounded, gp_Pnt(5.0, inset, inset), 1e-3);
    ASSERT_TRUE(bottomFace);
    const std::optional<TopoDS_Edge> fromBottom = blendSource(box, edges, 2.0, Blend::Fillet, *bottomFace);
    ASSERT_TRUE(fromBottom);
    EXPECT_TRUE(fromBottom->IsSame(*bottom));

    const std::optional<TopoDS_Face> side = findFace(rounded, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(side);
    EXPECT_FALSE(blendSource(box, edges, 2.0, Blend::Fillet, *side));
}

TEST(Topology, BisectorAndNormalPointWhereHandlesGo)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Edge> corner = findEdge(box, gp_Pnt(10.0, 10.0, 5.0), 1e-3);
    ASSERT_TRUE(corner);
    const std::optional<gp_Dir> inward = edgeBisector(box, *corner);
    ASSERT_TRUE(inward);
    EXPECT_TRUE(inward->IsEqual(gp_Dir(-1.0, -1.0, 0.0), 1e-9));

    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    const std::optional<gp_Dir> up = faceNormal(*top, gp_Pnt(5.0, 5.0, 10.0));
    ASSERT_TRUE(up);
    EXPECT_TRUE(up->IsEqual(gp_Dir(0.0, 0.0, 1.0), 1e-9));
}

TEST(Exchange, WritesStepAndStl)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-export";
    std::filesystem::create_directories(folder);
    const std::vector<TopoDS_Shape> shapes = {makeBox(10.0, 10.0, 10.0)};

    writeStep({{"Box", shapes.front(), std::nullopt}}, folder / "box.step");
    std::ifstream step(folder / "box.step");
    std::string header;
    std::getline(step, header);
    EXPECT_EQ(header.rfind("ISO-10303-21", 0), 0u);

    writeStl(shapes, folder / "box.stl");
    const auto size = std::filesystem::file_size(folder / "box.stl");
    EXPECT_EQ((size - 84) % 50, 0u);
    EXPECT_GE((size - 84) / 50, 12u);

    step.close();
    std::filesystem::remove_all(folder);
}

TEST(Exchange, ReadsBackWrittenStep)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-import";
    std::filesystem::create_directories(folder);
    const Color red{0.8, 0.1, 0.1};
    writeStep({{"Bracket \xC3\xA9", makeBox(10.0, 20.0, 30.0), red},
               {"Pin", makeCylinder(gp_Pnt(50.0, 0.0, 0.0), 5.0, 10.0), std::nullopt}},
              folder / "two.step");

    const std::vector<StepPart> parts = readStepParts(folder / "two.step");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].name, "Bracket \xC3\xA9");
    EXPECT_NEAR(volume(parts[0].shape), 6000.0, 1e-3);
    ASSERT_TRUE(parts[0].color);
    EXPECT_NEAR(parts[0].color->red, red.red, 1e-3);
    EXPECT_NEAR(parts[0].color->green, red.green, 1e-3);
    EXPECT_NEAR(parts[0].color->blue, red.blue, 1e-3);
    EXPECT_EQ(parts[1].name, "Pin");
    EXPECT_NEAR(volume(parts[1].shape), std::numbers::pi * 250.0, 1e-3);
    EXPECT_FALSE(parts[1].color);
    EXPECT_THROW(readStepParts(folder / "missing.step"), std::runtime_error);
    std::filesystem::remove_all(folder);
}

TEST(Exchange, StepKeepsAssemblyPaths)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-assembly";
    std::filesystem::create_directories(folder);
    const std::vector<std::string> rig{"Rig"};
    const std::vector<std::string> arm{"Rig", "Arm"};
    writeStep({{"Base", makeBox(10.0, 10.0, 10.0), std::nullopt, {}, rig},
               {"Pin", makeCylinder(gp_Pnt(50.0, 0.0, 0.0), 5.0, 10.0), std::nullopt, {}, arm},
               {"Loose", makeCylinder(gp_Pnt(0.0, 50.0, 0.0), 2.0, 10.0), std::nullopt, {}, {}}},
              folder / "rig.step");

    const std::vector<StepPart> parts = readStepParts(folder / "rig.step");
    ASSERT_EQ(parts.size(), 3u);
    const auto find = [&](const std::string& name) { return std::ranges::find(parts, name, &StepPart::name); };
    ASSERT_NE(find("Base"), parts.end());
    EXPECT_EQ(find("Base")->path, rig);
    ASSERT_NE(find("Pin"), parts.end());
    EXPECT_EQ(find("Pin")->path, arm);
    EXPECT_NEAR(areaCentroid(find("Pin")->shape).X(), 50.0, 1e-6);
    ASSERT_NE(find("Loose"), parts.end());
    EXPECT_TRUE(find("Loose")->path.empty());
    std::filesystem::remove_all(folder);
}

TEST(Exchange, StepHeaderNamesTheApplication)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-header";
    std::filesystem::create_directories(folder);
    writeStep({{"Box", makeBox(10.0, 20.0, 30.0), std::nullopt}}, folder / "box.step", "Kalve 137 9.9.9");
    std::ifstream in(folder / "box.step");
    std::string fileName;
    for (std::string line; std::getline(in, line);) {
        if (line.starts_with("FILE_NAME")) {
            fileName = line;
            for (std::string more; fileName.find(';') == std::string::npos && std::getline(in, more);) {
                fileName += more;
            }
            break;
        }
    }
    EXPECT_NE(fileName.find("'box.step'"), std::string::npos) << fileName;
    EXPECT_NE(fileName.find("'Kalve 137 9.9.9'"), std::string::npos) << fileName;
    in.close();
    std::filesystem::remove_all(folder);
}

TEST(Exchange, StepReadReportsProgressAndCancels)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-progress";
    std::filesystem::create_directories(folder);
    writeStep({{"Box", makeBox(10.0, 20.0, 30.0), std::nullopt}}, folder / "box.step");

    double last = -1.0;
    bool increasing = true;
    readStepParts(folder / "box.step", [&](double done) {
        increasing = increasing && done >= last;
        last = done;
        return true;
    });
    EXPECT_TRUE(increasing);
    EXPECT_GE(last, 0.0);
    EXPECT_LE(last, 1.0);

    EXPECT_THROW(readStepParts(folder / "box.step", [](double) { return false; }), ImportCanceled);
    std::filesystem::remove_all(folder);
}

TEST(FaceColors, FilletCarriesColorsOverAndLeavesNewFacesPlain)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    const std::optional<TopoDS_Face> bottom = findFace(box, gp_Pnt(5.0, 5.0, 0.0), 1e-3);
    ASSERT_TRUE(top);
    ASSERT_TRUE(bottom);
    const Color blue{0.2, 0.4, 0.8};
    const Color green{0.1, 0.7, 0.2};
    FaceColors colors;
    colors.set(*top, blue);
    colors.set(*bottom, green);

    std::vector<TopoDS_Edge> edges;
    for (TopExp_Explorer it(*top, TopAbs_EDGE); it.More(); it.Next()) {
        edges.push_back(TopoDS::Edge(it.Current()));
    }
    BRepTools_History history;
    const TopoDS_Shape rounded = fillet(box, edges, 2.0, &history);
    colors.remap(box, history);
    colors.prune(rounded);

    const std::optional<TopoDS_Face> roundedTop = findFace(rounded, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    const std::optional<TopoDS_Face> roundedBottom = findFace(rounded, gp_Pnt(5.0, 5.0, 0.0), 1e-3);
    ASSERT_TRUE(roundedTop);
    ASSERT_TRUE(roundedBottom);
    EXPECT_EQ(colors.find(*roundedTop), blue);
    EXPECT_EQ(colors.find(*roundedBottom), green);
    EXPECT_EQ(colors.size(), 2u);
}

TEST(FaceColors, CutDropsTheColorsOfRemovedFaces)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    FaceColors colors;
    colors.set(*top, Color{1.0, 0.0, 0.0});

    gp_Trsf up;
    up.SetTranslation(gp_Vec(0.0, 0.0, 5.0));
    BRepTools_History history;
    const TopoDS_Shape lower = cut(box, makeBox(10.0, 10.0, 10.0).Moved(TopLoc_Location(up)), &history);
    colors.remap(box, history);
    colors.prune(lower);

    EXPECT_TRUE(colors.empty());
}

TEST(FaceColors, PatternGivesEveryCopyTheColorsOfTheOriginal)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    const Color blue{0.2, 0.4, 0.8};
    FaceColors colors;
    colors.set(*top, blue);

    gp_Trsf aside;
    aside.SetTranslation(gp_Vec(20.0, 0.0, 0.0));
    BRepTools_History history;
    const TopoDS_Shape pair = pattern(box, {aside}, &history);
    colors.remap(box, history);
    colors.prune(pair);

    const std::optional<TopoDS_Face> first = findFace(pair, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    const std::optional<TopoDS_Face> second = findFace(pair, gp_Pnt(25.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(colors.find(*first), blue);
    EXPECT_EQ(colors.find(*second), blue);
}

TEST(FaceColors, IndicesRoundTripThroughTheSameShape)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    const Color blue{0.2, 0.4, 0.8};
    FaceColors colors;
    colors.set(*top, blue);

    const std::vector<std::pair<int, Color>> entries = colors.toIndices(box);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(FaceColors::fromIndices(box, entries), colors);
    EXPECT_TRUE(FaceColors::fromIndices(box, {{999, blue}}).empty());
}

TEST(FaceColors, MovedBodyKeepsThem)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    const Color blue{0.2, 0.4, 0.8};
    FaceColors colors;
    colors.set(*top, blue);

    gp_Trsf aside;
    aside.SetTranslation(gp_Vec(0.0, 0.0, 40.0));
    const TopoDS_Shape moved = box.Moved(TopLoc_Location(aside));
    const std::optional<TopoDS_Face> movedTop = findFace(moved, gp_Pnt(5.0, 5.0, 50.0), 1e-3);
    ASSERT_TRUE(movedTop);
    EXPECT_EQ(colors.find(*movedTop), blue);
}

TEST(Exchange, StepRoundTripsFaceColors)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-face-colors";
    std::filesystem::create_directories(folder);
    const TopoDS_Shape box = makeBox(10.0, 20.0, 30.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 10.0, 30.0), 1e-3);
    ASSERT_TRUE(top);
    const Color body{0.5, 0.5, 0.5};
    const Color blue{0.2, 0.4, 0.8};
    FaceColors colors;
    colors.set(*top, blue);
    writeStep({{"Plate", box, body, colors}}, folder / "plate.step");

    const std::vector<StepPart> parts = readStepParts(folder / "plate.step");
    ASSERT_EQ(parts.size(), 1u);
    ASSERT_EQ(parts[0].faceColors.size(), 1u);
    const std::optional<TopoDS_Face> readTop = findFace(parts[0].shape, gp_Pnt(5.0, 10.0, 30.0), 1e-3);
    ASSERT_TRUE(readTop);
    const std::optional<Color> read = parts[0].faceColors.find(*readTop);
    ASSERT_TRUE(read);
    EXPECT_NEAR(read->red, blue.red, 1e-3);
    EXPECT_NEAR(read->green, blue.green, 1e-3);
    EXPECT_NEAR(read->blue, blue.blue, 1e-3);
    std::filesystem::remove_all(folder);
}

TEST(Exchange, StepKeepsFaceColorsOfPartsInAssemblies)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-assembly-colors";
    std::filesystem::create_directories(folder);
    gp_Trsf aside;
    aside.SetTranslation(gp_Vec(50.0, 0.0, 0.0));
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0).Moved(TopLoc_Location(aside));
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(55.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    const Color blue{0.2, 0.4, 0.8};
    FaceColors colors;
    colors.set(*top, blue);
    writeStep({{"Plate", box, std::nullopt, colors, {"Rig"}}}, folder / "rig.step");

    const std::vector<StepPart> parts = readStepParts(folder / "rig.step");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].path, std::vector<std::string>{"Rig"});
    const std::optional<TopoDS_Face> readTop = findFace(parts[0].shape, gp_Pnt(55.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(readTop);
    const std::optional<Color> read = parts[0].faceColors.find(*readTop);
    ASSERT_TRUE(read);
    EXPECT_NEAR(read->blue, blue.blue, 1e-3);
    std::filesystem::remove_all(folder);
}

TEST(Exchange, BrepTextRoundTrips)
{
    const TopoDS_Shape box = makeBox(10.0, 20.0, 30.0);
    EXPECT_NEAR(volume(fromBrep(toBrep(box))), 6000.0, 1e-6);
    EXPECT_THROW(fromBrep("not a shape"), std::runtime_error);
}

TEST(Topology, BlocksViewOnlyWhenTheBodyIsInTheWay)
{
    const ViewBlockers box({makeBox(10.0, 10.0, 10.0)});
    const gp_Pnt onTop(5.0, 5.0, 10.0);
    EXPECT_FALSE(box.blocks(onTop, gp_Dir(0, 0, 1)));
    EXPECT_TRUE(box.blocks(gp_Pnt(5.0, 5.0, 0.0), gp_Dir(0, 0, 1)));
    EXPECT_FALSE(box.blocks(gp_Pnt(10.0, 0.0, 10.0), gp_Dir(1, -1, 1)));
    EXPECT_TRUE(box.blocks(gp_Pnt(0.0, 10.0, 0.0), gp_Dir(1, -1, 1)));
}

TEST(Topology, BlocksViewAcrossSeveralBodies)
{
    gp_Trsf aside;
    aside.SetTranslation(gp_Vec(0.0, 0.0, 20.0));
    const ViewBlockers stack({makeBox(10.0, 10.0, 10.0), makeBox(10.0, 10.0, 10.0).Moved(TopLoc_Location(aside))});
    EXPECT_TRUE(stack.blocks(gp_Pnt(5.0, 5.0, 10.0), gp_Dir(0, 0, 1)));
    EXPECT_FALSE(stack.blocks(gp_Pnt(5.0, 5.0, 30.0), gp_Dir(0, 0, 1)));
    EXPECT_FALSE(stack.blocks(gp_Pnt(15.0, 5.0, 0.0), gp_Dir(0, 0, 1)));
    EXPECT_FALSE(ViewBlockers({}).blocks(gp_Pnt(5.0, 5.0, 0.0), gp_Dir(0, 0, 1)));
}

TEST(Topology, SamplePointsLieOnTheShape)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Edge> edge = findEdge(box, gp_Pnt(5.0, 0.0, 0.0), 1e-6);
    ASSERT_TRUE(edge);
    const std::vector<gp_Pnt> points = samplePoints(*edge);
    ASSERT_EQ(points.size(), 5u);
    for (const gp_Pnt& point : points) {
        EXPECT_NEAR(point.Y(), 0.0, 1e-9);
        EXPECT_NEAR(point.Z(), 0.0, 1e-9);
        EXPECT_GT(point.X(), 0.0);
        EXPECT_LT(point.X(), 10.0);
    }
    EXPECT_TRUE(samplePoints(box).empty());
}

TEST(Topology, FaceSamplesSeePastAShadeOverItsMiddle)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::optional<TopoDS_Face> top = findFace(box, gp_Pnt(5.0, 5.0, 10.0), 1e-3);
    ASSERT_TRUE(top);
    gp_Trsf above;
    above.SetTranslation(gp_Vec(0.0, 0.0, 12.0));
    const ViewBlockers shade({makeBox(6.0, 10.0, 2.0).Moved(TopLoc_Location(above))});
    const gp_Dir up(0, 0, 1);

    const std::vector<gp_Pnt> points = samplePoints(*top);
    ASSERT_GT(points.size(), 1u);
    EXPECT_TRUE(shade.blocks(points.front(), up));
    EXPECT_TRUE(std::ranges::any_of(points, [&](const gp_Pnt& point) { return !shade.blocks(point, up); }));
    for (const gp_Pnt& point : points) {
        EXPECT_NEAR(point.Z(), 10.0, 1e-9);
    }
}

} // namespace
