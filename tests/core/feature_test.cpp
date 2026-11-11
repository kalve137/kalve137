// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "geometry/exchange.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <nlohmann/json.hpp>

#include <cmath>
#include <numbers>

using namespace kalve137;
using namespace kalve137::core;

namespace {

constexpr double kPi = std::numbers::pi;

double totalVolume(const Model& model)
{
    double total = 0.0;
    for (const Body& body : model.bodies) {
        total += geometry::volume(body.shape);
    }
    return total;
}

FeatureId addBlock(Document& doc)
{
    return doc.add(ImportFeature{"block.step", {{"Block", geometry::makeBox(40.0, 20.0, 10.0), std::nullopt}}});
}

bool hasVertexAt(const TopoDS_Shape& shape, const gp_Pnt& point)
{
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next()) {
        if (BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).Distance(point) < 1e-6) {
            return true;
        }
    }
    return false;
}

HoleFeature faceHole(double diameter, double depth)
{
    HoleFeature hole;
    hole.facePoints = {gp_Pnt(20, 10, 10)};
    hole.diameter = diameter;
    hole.depth = depth;
    hole.tipAngle = 0.0;
    return hole;
}

double removedBy(const HoleFeature& hole)
{
    Document doc;
    addBlock(doc);
    const FeatureId id = doc.add(hole);
    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
    return 8000.0 - totalVolume(model);
}

} // namespace

TEST(OffsetFace, PushesAndPullsAFlatFace)
{
    for (const double distance : {5.0, -5.0}) {
        Document doc;
        addBlock(doc);
        const FeatureId id = doc.add(OffsetFaceFeature{{gp_Pnt(20, 10, 10)}, distance});
        const Model model = recompute(doc);
        EXPECT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
        EXPECT_NEAR(totalVolume(model), 40.0 * 20.0 * (10.0 + distance), 1e-3);
    }
}

TEST(OffsetFace, RefusesCurvedFaces)
{
    Document doc;
    doc.add(ImportFeature{"pin.step", {{"Pin", geometry::makeCylinder(gp_Pnt(0, 0, 0), 5.0, 10.0), std::nullopt}}});
    const FeatureId id = doc.add(OffsetFaceFeature{{gp_Pnt(5, 0, 5)}, 2.0});
    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(id).health, FeatureHealth::Failed);
    EXPECT_EQ(model.status.at(id).message, "Press Pull moves flat faces only");
}

TEST(Hole, SimpleHoleWithFlatAndPointedBottom)
{
    const double r = 2.5;
    EXPECT_NEAR(removedBy(faceHole(5.0, 6.0)), kPi * r * r * 6.0, 1e-3);
    HoleFeature pointed = faceHole(5.0, 6.0);
    pointed.tipAngle = 118.0;
    const double tip = r / std::tan(59.0 * kPi / 180.0);
    EXPECT_NEAR(removedBy(pointed), kPi * r * r * (6.0 + tip / 3.0), 1e-3);
}

TEST(Hole, CounterboreAndCountersink)
{
    HoleFeature bore = faceHole(5.0, 8.0);
    bore.kind = HoleKind::Counterbore;
    bore.headDiameter = 9.0;
    bore.headDepth = 3.0;
    EXPECT_NEAR(removedBy(bore), kPi * (4.5 * 4.5 * 3.0 + 2.5 * 2.5 * 5.0), 1e-3);

    HoleFeature sink = faceHole(5.0, 8.0);
    sink.kind = HoleKind::Countersink;
    sink.headDiameter = 9.0;
    sink.countersinkAngle = 90.0;
    const double cone = kPi * 2.0 / 3.0 * (4.5 * 4.5 + 4.5 * 2.5 + 2.5 * 2.5);
    EXPECT_NEAR(removedBy(sink), cone + kPi * 2.5 * 2.5 * 6.0, 1e-3);

    HoleFeature tooSmall = bore;
    tooSmall.headDiameter = 4.0;
    Document doc;
    addBlock(doc);
    const FeatureId id = doc.add(tooSmall);
    EXPECT_EQ(recompute(doc).status.at(id).health, FeatureHealth::Failed);
}

TEST(Hole, ThroughAllCutsTheWholeBlock)
{
    HoleFeature hole = faceHole(4.0, 1.0);
    hole.throughAll = true;
    hole.tipAngle = 118.0;
    EXPECT_NEAR(removedBy(hole), kPi * 4.0 * 10.0, 1e-3);
}

TEST(Hole, SketchPointHoleFollowsThePoint)
{
    Document doc;
    addBlock(doc);
    SketchFeature sketch;
    sketch.plane = gp_Ax3(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    sketch.planeName = "Face";
    const sketch::Id point = sketch.sketch.addPoint({10, 10});
    const FeatureId sketchId = doc.add(std::move(sketch));
    HoleFeature hole;
    hole.sketch = sketchId;
    hole.sketchPoints = {point};
    hole.diameter = 5.0;
    hole.throughAll = true;
    doc.add(hole);

    Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_TRUE(geometry::findFace(model.bodies[0].shape, gp_Pnt(12.5, 10, 5), 1e-6));

    std::get<SketchFeature>(doc.find(sketchId)->data).sketch.point(point)->pos = {30, 10};
    model = recompute(doc);
    EXPECT_TRUE(geometry::findFace(model.bodies[0].shape, gp_Pnt(32.5, 10, 5), 1e-6));
    EXPECT_FALSE(geometry::findFace(model.bodies[0].shape, gp_Pnt(12.5, 10, 5), 1e-6));
    EXPECT_NEAR(totalVolume(model), 8000.0 - kPi * 6.25 * 10.0, 1e-3);
}

TEST(Move, TranslatesAndRotatesKeepingTheKey)
{
    Document doc;
    const FeatureId block =
        doc.add(ImportFeature{"cube.step", {{"Cube", geometry::makeBox(10.0, 10.0, 10.0), std::nullopt}}});
    MoveFeature move;
    move.bodies = {{block, 0}};
    move.translation = gp_Vec(5, 0, 0);
    move.rotateAxis = AxisKind::Z;
    move.angleDegrees = 90.0;
    move.pivot = gp_Pnt(5, 5, 5);
    doc.add(move);

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_EQ(model.bodies[0].key(), (BodyKey{block, 0}));
    EXPECT_TRUE(hasVertexAt(model.bodies[0].shape, gp_Pnt(15, 0, 0)));
}

TEST(Move, CopyAddsABodyAndKeepsTheSource)
{
    Document doc;
    const FeatureId block =
        doc.add(ImportFeature{"cube.step", {{"Cube", geometry::makeBox(10.0, 10.0, 10.0), geometry::Color{1, 0, 0}}}});
    MoveFeature move;
    move.bodies = {{block, 0}};
    move.translation = gp_Vec(0, 20, 0);
    move.copy = true;
    const FeatureId moveId = doc.add(move);

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 2u);
    EXPECT_EQ(model.bodies[0].key(), (BodyKey{block, 0}));
    EXPECT_TRUE(hasVertexAt(model.bodies[0].shape, gp_Pnt(0, 0, 0)));
    EXPECT_EQ(model.bodies[1].key(), (BodyKey{moveId, 0}));
    EXPECT_EQ(model.bodies[1].name, "Cube");
    EXPECT_EQ(model.bodies[1].color, model.bodies[0].color);
    EXPECT_TRUE(hasVertexAt(model.bodies[1].shape, gp_Pnt(0, 20, 0)));
}

TEST(Move, CopyTakesItsAcceptedName)
{
    Document doc;
    const FeatureId block = doc.add(ImportFeature{"cube.step", {{"Cube", geometry::makeBox(10.0, 10.0, 10.0)}}});
    MoveFeature move;
    move.bodies = {{block, 0}};
    move.translation = gp_Vec(0, 20, 0);
    move.copy = true;
    move.copyNames = {"Body2"};
    doc.add(move);

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 2u);
    EXPECT_EQ(model.bodies[0].name, "Cube");
    EXPECT_EQ(model.bodies[1].name, "Body2");
}

TEST(Move, LaterFilletFindsTheMovedEdge)
{
    Document doc;
    const FeatureId block =
        doc.add(ImportFeature{"cube.step", {{"Cube", geometry::makeBox(10.0, 10.0, 10.0), std::nullopt}}});
    MoveFeature move;
    move.bodies = {{block, 0}};
    move.translation = gp_Vec(20, 0, 0);
    doc.add(move);
    const FeatureId fillet = doc.add(FilletFeature{{FilletSet{{gp_Pnt(25, 0, 10)}, 1.0}}});

    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(fillet).health, FeatureHealth::Ok) << model.status.at(fillet).message;
    EXPECT_LT(totalVolume(model), 1000.0 - 1e-3);
}

TEST(Construction, PlaneAtAngleTurnsAboutTheLine)
{
    PlaneFeature plane;
    plane.kind = PlaneKind::Angle;
    plane.base = originPlane(OriginPlane::XY);
    plane.baseName = "X";
    plane.hinge = gp_Ax1(gp::Origin(), gp::DX());
    plane.angleDegrees = 90.0;
    const ConstructionPlane placed = constructionPlane(plane);
    EXPECT_TRUE(placed.frame.Direction().IsEqual(gp_Dir(0, -1, 0), 1e-9));
    EXPECT_TRUE(placed.frame.XDirection().IsEqual(gp::DX(), 1e-9));
    EXPECT_TRUE(placed.frame.Location().IsEqual(gp::Origin(), 1e-9));
}

TEST(Construction, MidplaneOfParallelAndCrossingPicks)
{
    PlaneFeature parallel;
    parallel.kind = PlaneKind::Midplane;
    parallel.base = originPlane(OriginPlane::XY);
    parallel.baseName = "XY";
    parallel.second = gp_Ax3(gp_Pnt(3, 4, 10), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0));
    parallel.secondName = "Face";
    parallel.secondCenter = gp_Pnt(20, 10, 10);
    const ConstructionPlane middle = constructionPlane(parallel);
    EXPECT_NEAR(middle.frame.Location().Z(), 5.0, 1e-9);
    EXPECT_TRUE(middle.frame.Direction().IsParallel(gp::DZ(), 1e-9));
    EXPECT_NEAR(middle.center.Z(), 5.0, 1e-9);

    PlaneFeature crossing;
    crossing.kind = PlaneKind::Midplane;
    crossing.base = gp_Ax3(gp_Pnt(10, 0, 0), gp::DX(), gp::DY());
    crossing.baseName = "Face";
    crossing.second = gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX());
    crossing.secondName = "Face";
    const ConstructionPlane bisector = constructionPlane(crossing);
    EXPECT_TRUE(bisector.frame.Location().IsEqual(gp_Pnt(10, 0, 10), 1e-9));
    EXPECT_TRUE(bisector.frame.Direction().IsParallel(gp_Dir(1, 0, -1), 1e-9));
    EXPECT_TRUE(bisector.frame.XDirection().IsParallel(gp::DY(), 1e-9));

    crossing.secondName.clear();
    EXPECT_THROW(constructionPlane(crossing), std::runtime_error);
}

TEST(Construction, AxisThroughTwoPoints)
{
    const auto axis = axisThrough(gp_Pnt(1, 2, 3), gp_Pnt(1, 2, 13));
    ASSERT_TRUE(axis);
    EXPECT_TRUE(axis->Location().IsEqual(gp_Pnt(1, 2, 3), 1e-12));
    EXPECT_TRUE(axis->Direction().IsEqual(gp::DZ(), 1e-12));
    EXPECT_FALSE(axisThrough(gp_Pnt(1, 2, 3), gp_Pnt(1, 2, 3)));
}

TEST(Construction, RevolveAboutAConstructionAxis)
{
    Document doc;
    const FeatureId axis = doc.add(AxisFeature{AxisSource::TwoPoints, gp_Ax1(gp::Origin(), gp::DY()), "Points"});
    SketchFeature data;
    sketch::Sketch& s = data.sketch;
    const auto a = s.addPoint({10, 0});
    const auto b = s.addPoint({20, 0});
    const auto c = s.addPoint({20, 10});
    const auto d = s.addPoint({10, 10});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
    const FeatureId sketchId = doc.add(std::move(data));
    RevolveFeature revolve;
    revolve.profiles = {sketchId, {{15, 5}}};
    revolve.axis.kind = AxisKind::Construction;
    revolve.axis.construction = axis;
    revolve.bodyName = "Body1";
    const FeatureId revolveId = doc.add(revolve);

    Model model = recompute(doc);
    EXPECT_EQ(model.status.at(revolveId).health, FeatureHealth::Ok) << model.status.at(revolveId).message;
    EXPECT_NEAR(totalVolume(model), kPi * (400.0 - 100.0) * 10.0, 1e-3);

    doc.find(axis)->suppressed = true;
    model = recompute(doc);
    EXPECT_EQ(model.status.at(revolveId).health, FeatureHealth::Failed);
}

TEST(Appearance, BodyColorReplacesFaceColors)
{
    Document doc;
    const FeatureId block = addBlock(doc);
    doc.add(AppearanceFeature{{}, {gp_Pnt(20, 10, 10)}, geometry::Color{0, 0, 1}});
    const FeatureId whole = doc.add(AppearanceFeature{{{block, 0}}, {}, geometry::Color{1, 0, 0}});

    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(whole).health, FeatureHealth::Ok) << model.status.at(whole).message;
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_EQ(model.bodies[0].color, (geometry::Color{1, 0, 0}));
    EXPECT_TRUE(model.bodies[0].faceColors.empty());
}

TEST(Appearance, FaceColorSurvivesLaterFeatures)
{
    Document doc;
    addBlock(doc);
    const geometry::Color blue{0.2, 0.4, 0.8};
    doc.add(AppearanceFeature{{}, {gp_Pnt(20, 10, 10)}, blue});
    doc.add(OffsetFaceFeature{{gp_Pnt(20, 10, 10)}, 5.0});
    doc.add(faceHole(5.0, 4.0));

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    const auto top = geometry::findFace(model.bodies[0].shape, gp_Pnt(5, 5, 15), 1e-3);
    ASSERT_TRUE(top);
    EXPECT_EQ(model.bodies[0].faceColors.find(*top), blue);
    EXPECT_EQ(model.bodies[0].faceColors.size(), 1u);
}

TEST(Appearance, CopiedBodyCarriesTheFaceColor)
{
    Document doc;
    const FeatureId block =
        doc.add(ImportFeature{"cube.step", {{"Cube", geometry::makeBox(10.0, 10.0, 10.0), std::nullopt}}});
    const geometry::Color blue{0.2, 0.4, 0.8};
    doc.add(AppearanceFeature{{}, {gp_Pnt(5, 5, 10)}, blue});
    MoveFeature move;
    move.bodies = {{block, 0}};
    move.translation = gp_Vec(0, 20, 0);
    move.copy = true;
    doc.add(move);

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 2u);
    const auto copied = geometry::findFace(model.bodies[1].shape, gp_Pnt(5, 25, 10), 1e-3);
    ASSERT_TRUE(copied);
    EXPECT_EQ(model.bodies[1].faceColors.find(*copied), blue);
}

TEST(Appearance, NoColorPutsTheFaceBack)
{
    Document doc;
    addBlock(doc);
    doc.add(AppearanceFeature{{}, {gp_Pnt(20, 10, 10)}, geometry::Color{0, 0, 1}});
    const FeatureId back = doc.add(AppearanceFeature{{}, {gp_Pnt(20, 10, 10)}, std::nullopt, true});

    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(back).health, FeatureHealth::Ok) << model.status.at(back).message;
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_TRUE(model.bodies[0].faceColors.empty());
}

TEST(Appearance, WaitsForAColor)
{
    Document doc;
    const FeatureId block = addBlock(doc);
    const FeatureId waiting = doc.add(AppearanceFeature{{{block, 0}}, {}, std::nullopt});

    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(waiting).health, FeatureHealth::Failed);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_FALSE(model.bodies[0].color);
}

TEST(Appearance, ReportsTargetsThatAreGone)
{
    Document doc;
    addBlock(doc);
    const FeatureId lost = doc.add(AppearanceFeature{{}, {gp_Pnt(500, 500, 500)}, geometry::Color{0, 0, 1}});

    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(lost).health, FeatureHealth::Failed);
}

TEST(Serialization, AppearanceRoundTrips)
{
    Document doc;
    const FeatureId block = addBlock(doc);
    doc.add(AppearanceFeature{{{block, 0}}, {gp_Pnt(20, 10, 10)}, geometry::Color{0.25, 0.5, 0.75}});
    doc.add(AppearanceFeature{{}, {gp_Pnt(20, 10, 0)}, std::nullopt, true});

    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    const auto& colored = std::get<AppearanceFeature>(loaded.features()[1].data);
    ASSERT_TRUE(colored.color);
    EXPECT_NEAR(colored.color->green, 0.5, 1e-12);
    EXPECT_EQ(colored.bodies.size(), 1u);
    EXPECT_EQ(colored.faces.size(), 1u);
    const auto& back = std::get<AppearanceFeature>(loaded.features()[2].data);
    EXPECT_FALSE(back.color);
    EXPECT_TRUE(back.clear);
}

TEST(Serialization, VersionFourImportLoadsWithoutFaceColors)
{
    using nlohmann::json;
    ShapeStore shapes;
    const std::string key = shapes.put(geometry::makeBox(10.0, 10.0, 10.0));
    const json part = {{"name", "Cube"}, {"shape", key}, {"color", {1.0, 0.0, 0.0}}};
    const json data = {{"source", "cube.step"}, {"parts", json::array({part})}};
    const json feature = {{"id", 1}, {"type", "import"}, {"name", "Cube"}, {"suppressed", false}, {"data", data}};
    const json legacy = {{"format", "kalve137-design"},
                         {"version", 4},
                         {"marker", 1},
                         {"next_id", 2},
                         {"features", json::array({feature})}};

    const Document doc = fromJson(legacy.dump(), shapes);
    const auto& imported = std::get<ImportFeature>(doc.features().front().data);
    ASSERT_EQ(imported.parts.size(), 1u);
    EXPECT_TRUE(imported.parts[0].faceColors.empty());
    ASSERT_TRUE(imported.parts[0].color);
    EXPECT_NEAR(imported.parts[0].color->red, 1.0, 1e-12);
}

TEST(Serialization, NewFeatureTypesRoundTrip)
{
    Document doc;
    const FeatureId block = addBlock(doc);
    doc.add(OffsetFaceFeature{{gp_Pnt(20, 10, 10)}, -2.0});
    HoleFeature hole = faceHole(3.0, 4.0);
    hole.kind = HoleKind::Countersink;
    doc.add(hole);
    MoveFeature move;
    move.bodies = {{block, 0}};
    move.translation = gp_Vec(1, 2, 3);
    move.rotateAxis = AxisKind::Y;
    move.angleDegrees = 30.0;
    move.pivot = gp_Pnt(20, 10, 5);
    move.copy = true;
    move.copyNames = {"Body2"};
    doc.add(move);
    PlaneFeature angle;
    angle.kind = PlaneKind::Angle;
    angle.baseName = "X";
    angle.hinge = gp_Ax1(gp::Origin(), gp::DX());
    angle.angleDegrees = 30.0;
    doc.add(angle);
    PlaneFeature middle;
    middle.kind = PlaneKind::Midplane;
    middle.baseName = "XY";
    middle.second = gp_Ax3(gp_Pnt(0, 0, 8), gp::DZ(), gp::DX());
    middle.secondName = "Face";
    doc.add(middle);
    const FeatureId axis = doc.add(AxisFeature{AxisSource::Cylinder, gp_Ax1(gp_Pnt(1, 1, 0), gp::DZ()), "Cylinder"});
    doc.add(PointFeature{gp_Pnt(4, 5, 6), "Vertex"});
    PatternFeature pattern;
    pattern.body = {block, 0};
    pattern.kind = PatternKind::Circular;
    pattern.axis.kind = AxisKind::Construction;
    pattern.axis.construction = axis;
    doc.add(pattern);

    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    EXPECT_EQ(loaded.features()[1].name, "OffsetFace1");
    EXPECT_EQ(std::get<PlaneFeature>(loaded.features()[4].data).kind, PlaneKind::Angle);
    EXPECT_EQ(std::get<MoveFeature>(loaded.features()[3].data).copyNames, std::vector<std::string>{"Body2"});
}

TEST(Serialization, VersionThreeOffsetPlaneLoads)
{
    using nlohmann::json;
    const json base = {{"origin", {0.0, 0.0, 0.0}}, {"normal", {0.0, 0.0, 1.0}}, {"x_dir", {1.0, 0.0, 0.0}}};
    const json data = {{"base", base}, {"base_name", "XY"}, {"center", {0.0, 0.0, 0.0}}, {"offset", 12.5}};
    const json feature = {{"id", 1}, {"type", "plane"}, {"name", "Plane1"}, {"suppressed", false}, {"data", data}};
    const json legacy = {{"format", "kalve137-design"},
                         {"version", 3},
                         {"marker", 1},
                         {"next_id", 2},
                         {"features", json::array({feature})}};

    ShapeStore shapes;
    const Document doc = fromJson(legacy.dump(), shapes);
    const auto& plane = std::get<PlaneFeature>(doc.features().front().data);
    EXPECT_EQ(plane.kind, PlaneKind::Offset);
    EXPECT_NEAR(recompute(doc).planes.at(1).frame.Location().Z(), 12.5, 1e-12);
}
