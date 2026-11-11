// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/project.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "core/undo_stack.h"
#include "geometry/exchange.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <numbers>

using namespace kalve137;
using namespace kalve137::core;

namespace {

FeatureId addBaseSketch(Document& doc)
{
    SketchFeature data;
    sketch::Sketch& s = data.sketch;
    const auto a = s.addPoint({0, 0});
    const auto b = s.addPoint({40, 0});
    const auto c = s.addPoint({40, 20});
    const auto d = s.addPoint({0, 20});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
    return doc.add(std::move(data));
}

FeatureId addHoleSketch(Document& doc, double radius)
{
    SketchFeature data;
    data.sketch.addCircle(data.sketch.addPoint({20, 10}), radius);
    return doc.add(std::move(data));
}

FeatureId addExtrude(Document& doc, FeatureId sketch, sketch::Vec2 seed, double distance, Operation op)
{
    ExtrudeFeature data;
    data.profiles = {sketch, {seed}};
    data.distance = distance;
    data.operation = op;
    if (op == Operation::NewBody || op == Operation::Join) {
        data.bodyName = doc.takeBodyName();
    }
    return doc.add(std::move(data));
}

double totalVolume(const Model& model)
{
    double total = 0.0;
    for (const Body& body : model.bodies) {
        total += geometry::volume(body.shape);
    }
    return total;
}

Document blockWithHole()
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    const FeatureId hole = addHoleSketch(doc, 5.0);
    addExtrude(doc, hole, {20, 10}, 10.0, Operation::Cut);
    return doc;
}

const double kBlockWithHoleVolume = 40.0 * 20.0 * 10.0 - std::numbers::pi * 25.0 * 10.0;

} // namespace

TEST(Document, DefaultNamesAndMarker)
{
    Document doc;
    const FeatureId sketch = addBaseSketch(doc);
    const FeatureId extrude = addExtrude(doc, sketch, {5, 5}, 10.0, Operation::NewBody);
    EXPECT_EQ(doc.find(sketch)->name, "Sketch1");
    EXPECT_EQ(doc.find(extrude)->name, "Extrude1");
    EXPECT_EQ(doc.marker(), 2u);

    doc.setMarker(1);
    const FeatureId inserted = addBaseSketch(doc);
    EXPECT_EQ(doc.indexOf(inserted), 1);
    EXPECT_EQ(doc.marker(), 2u);
    EXPECT_TRUE(doc.isRolledBack(extrude));
}

TEST(Recompute, ExtrudeAndCut)
{
    const Model model = recompute(blockWithHole());
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_EQ(model.bodies[0].name, "Body1");
    EXPECT_NEAR(totalVolume(model), kBlockWithHoleVolume, 1e-3);
    for (const auto& [id, status] : model.status) {
        EXPECT_EQ(status.health, FeatureHealth::Ok) << status.message;
    }
}

TEST(Recompute, SymmetricAndReversedExtrude)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    const FeatureId extrude = addExtrude(doc, base, {5, 5}, -10.0, Operation::NewBody);
    std::get<ExtrudeFeature>(doc.find(extrude)->data).extent = ExtrudeExtent::Symmetric;
    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_NEAR(totalVolume(model), 8000.0, 1e-3);
}

TEST(Recompute, TwoSidedExtrudeSpansBothDistances)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    const FeatureId extrude = addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    auto& data = std::get<ExtrudeFeature>(doc.find(extrude)->data);
    data.extent = ExtrudeExtent::TwoSides;
    data.secondDistance = 5.0;
    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_NEAR(totalVolume(model), 12000.0, 1e-3);
    EXPECT_TRUE(geometry::findFace(model.bodies[0].shape, gp_Pnt(20, 10, -5), 1e-6).has_value());

    ShapeStore shapes;
    const Document loaded = fromJson(toJson(doc, shapes), shapes);
    const auto& reloaded = std::get<ExtrudeFeature>(loaded.find(extrude)->data);
    EXPECT_EQ(reloaded.extent, ExtrudeExtent::TwoSides);
    EXPECT_DOUBLE_EQ(reloaded.secondDistance, 5.0);
}

TEST(Recompute, ExtendMatchesRecomputeAndKeepsUntouchedBodies)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    const Model before = recompute(doc);

    SketchFeature far;
    far.plane = gp_Ax3(gp_Pnt(0, 100, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    far.planeName = "Face";
    far.sketch.addCircle(far.sketch.addPoint({0, 0}), 5.0);
    const FeatureId circle = doc.add(std::move(far));
    const Model withSketch = extend(before, *doc.find(circle));
    const FeatureId pad = addExtrude(doc, circle, {0, 0}, 10.0, Operation::NewBody);

    const Model extended = extend(withSketch, *doc.find(pad));
    const Model full = recompute(doc);
    ASSERT_EQ(extended.bodies.size(), full.bodies.size());
    EXPECT_NEAR(totalVolume(extended), totalVolume(full), 1e-6);
    EXPECT_EQ(extended.status.at(pad).health, FeatureHealth::Ok);
    EXPECT_TRUE(extended.bodies[0].shape.IsSame(before.bodies[0].shape));
}

TEST(Recompute, JoinFusesTouchingBodies)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    SketchFeature top;
    top.plane = gp_Ax3(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    top.planeName = "Face";
    top.sketch.addCircle(top.sketch.addPoint({20, 10}), 5.0);
    const FeatureId boss = doc.add(std::move(top));
    addExtrude(doc, boss, {20, 10}, 5.0, Operation::Join);
    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_NEAR(totalVolume(model), 8000.0 + std::numbers::pi * 25.0 * 5.0, 1e-3);
}

TEST(Recompute, RollbackHidesLaterFeatures)
{
    Document doc = blockWithHole();
    doc.setMarker(2);
    const Model model = recompute(doc);
    EXPECT_NEAR(totalVolume(model), 8000.0, 1e-3);
    EXPECT_EQ(model.status.at(doc.features()[3].id).health, FeatureHealth::RolledBack);
}

TEST(Recompute, SuppressedFeatureIsSkipped)
{
    Document doc = blockWithHole();
    doc.find(doc.features()[3].id)->suppressed = true;
    EXPECT_NEAR(totalVolume(recompute(doc)), 8000.0, 1e-3);
}

TEST(Recompute, MissingProfileFailsOnlyThatFeature)
{
    Document doc = blockWithHole();
    auto& cut = std::get<ExtrudeFeature>(doc.find(doc.features()[3].id)->data);
    cut.profiles.seeds = {{100, 100}};
    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(doc.features()[3].id).health, FeatureHealth::Failed);
    EXPECT_NEAR(totalVolume(model), 8000.0, 1e-3);
}

TEST(Recompute, RevolveAroundSketchLine)
{
    Document doc;
    SketchFeature data;
    data.plane = originPlane(OriginPlane::XZ);
    sketch::Sketch& s = data.sketch;
    const auto a = s.addPoint({0, 0});
    const auto b = s.addPoint({10, 0});
    const auto c = s.addPoint({10, 30});
    const auto d = s.addPoint({0, 30});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    const auto axisLine = s.addLine(d, a);
    const FeatureId sketch = doc.add(std::move(data));

    RevolveFeature revolve;
    revolve.profiles = {sketch, {{5, 15}}};
    revolve.axis = {AxisKind::SketchLine, sketch, axisLine};
    revolve.bodyName = doc.takeBodyName();
    doc.add(std::move(revolve));

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_NEAR(totalVolume(model), std::numbers::pi * 100.0 * 30.0, 1e-2);
}

TEST(Recompute, FilletAndChamferFindEdgesByPointOnEdge)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    doc.add(FilletFeature{{FilletSet{{gp_Pnt(0, 0, 5)}, 2.0}}});
    doc.add(ChamferFeature{{gp_Pnt(20, 0, 10)}, 1.0});

    const Model model = recompute(doc);
    for (const auto& [id, status] : model.status) {
        EXPECT_EQ(status.health, FeatureHealth::Ok) << status.message;
    }
    const double filletLoss = (4.0 - std::numbers::pi) * 10.0;
    const double chainLength = 38.0 + std::numbers::pi + 18.0;
    EXPECT_NEAR(totalVolume(model), 8000.0 - filletLoss - 0.5 * chainLength, 0.5);
}

TEST(Recompute, ShellOpensTopFaceInside)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    doc.add(ShellFeature{{gp_Pnt(10, 20, 10)}, 2.0, true});

    const Model model = recompute(doc);
    for (const auto& [id, status] : model.status) {
        EXPECT_EQ(status.health, FeatureHealth::Ok) << status.message;
    }
    EXPECT_NEAR(totalVolume(model), 8000.0 - 16.0 * 36.0 * 8.0, 1e-3);
}

TEST(Recompute, ShellOutsideKeepsCavityAtBodySize)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    doc.add(ShellFeature{{gp_Pnt(10, 20, 10)}, 2.0, false});

    const Model model = recompute(doc);
    EXPECT_NEAR(totalVolume(model), 24.0 * 44.0 * 12.0 - 8000.0, 1e-3);
}

TEST(Recompute, ShellReportsMissingFaces)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    const FeatureId shell = doc.add(ShellFeature{{gp_Pnt(100, 100, 100)}, 2.0, true});

    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(shell).health, FeatureHealth::Failed);
    EXPECT_NEAR(totalVolume(model), 8000.0, 1e-3);
}

TEST(Recompute, ShellOpensAFaceWhereARoundMeetsAChamfer)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 20.0, Operation::NewBody);
    doc.add(FilletFeature{{FilletSet{{gp_Pnt(20, 0, 20)}, 3.0}}});
    doc.add(ChamferFeature{{gp_Pnt(40, 10, 20)}, 2.0});
    const FeatureId shell = doc.add(ShellFeature{{gp_Pnt(40, 10, 5)}, 2.0, true});

    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(shell).health, FeatureHealth::Ok) << model.status.at(shell).message;
    const TopoDS_Shape& body = model.bodies.at(0).shape;
    const auto state = [&](const gp_Pnt& point) {
        BRepClass3d_SolidClassifier classifier(body, point, 1e-6);
        return classifier.State();
    };
    EXPECT_EQ(state(gp_Pnt(20, 10, 10)), TopAbs_OUT);
    EXPECT_EQ(state(gp_Pnt(39, 10, 10)), TopAbs_OUT);
    EXPECT_EQ(state(gp_Pnt(20, 10, 19)), TopAbs_IN);
    EXPECT_EQ(state(gp_Pnt(20, 1, 10)), TopAbs_IN);
    EXPECT_EQ(state(gp_Pnt(1, 10, 10)), TopAbs_IN);
    EXPECT_EQ(state(gp_Pnt(20, 10, 1)), TopAbs_IN);
}

TEST(Recompute, ShellFailsRatherThanDroppingWalls)
{
    for (const bool withHole : {false, true}) {
        SCOPED_TRACE(withHole ? "with hole" : "without hole");
        Document doc;
        SketchFeature data;
        sketch::Sketch& s = data.sketch;
        const auto a = s.addPoint({0, 0});
        const auto b = s.addPoint({60, 0});
        const auto c = s.addPoint({60, 30});
        const auto d = s.addPoint({0, 30});
        s.addLine(a, b);
        s.addLine(b, c);
        s.addLine(c, d);
        s.addLine(d, a);
        const FeatureId base = doc.add(std::move(data));
        addExtrude(doc, base, {5, 5}, 20.0, Operation::NewBody);
        doc.add(FilletFeature{{FilletSet{{gp_Pnt(30, 0, 20)}, 3.0}}});
        doc.add(ChamferFeature{{gp_Pnt(60, 15, 20),
                                gp_Pnt(60, 3.0 - 1.5 * std::numbers::sqrt2, 17.0 + 1.5 * std::numbers::sqrt2),
                                gp_Pnt(60, 0, 8.5)},
                               2.0});
        if (withHole) {
            HoleFeature hole;
            hole.facePoints = {gp_Pnt(30, 15, 20)};
            hole.diameter = 5.0;
            hole.depth = 10.0;
            doc.add(hole);
        }
        const FeatureId shell = doc.add(ShellFeature{{gp_Pnt(60, 15, 5)}, 2.0, true});

        const Model model = recompute(doc);
        if (model.status.at(shell).health != FeatureHealth::Ok) {
            EXPECT_EQ(model.status.at(shell).health, FeatureHealth::Failed);
            EXPECT_FALSE(model.status.at(shell).message.empty());
            continue;
        }
        const TopoDS_Shape& body = model.bodies.at(0).shape;
        const auto state = [&](const gp_Pnt& point) {
            BRepClass3d_SolidClassifier classifier(body, point, 1e-6);
            return classifier.State();
        };
        EXPECT_EQ(state(gp_Pnt(15, 15, 10)), TopAbs_OUT);
        for (const gp_Pnt& wall : {gp_Pnt(15, 15, 1), gp_Pnt(15, 29, 10), gp_Pnt(15, 1, 10), gp_Pnt(1, 15, 10)}) {
            EXPECT_EQ(state(wall), TopAbs_IN) << wall.X() << " " << wall.Y() << " " << wall.Z();
        }
    }
}
TEST(Recompute, SketchFollowsItsOffsetPlane)
{
    Document doc;
    PlaneFeature offset;
    offset.base = originPlane(OriginPlane::XZ);
    offset.baseName = "XZ";
    offset.offset = 15.0;
    const FeatureId plane = doc.add(offset);
    const FeatureId base = addBaseSketch(doc);
    auto& sketch = std::get<SketchFeature>(doc.find(base)->data);
    sketch.planeFeature = plane;
    sketch.planeName = "Plane1";
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);

    Model model = recompute(doc);
    ASSERT_EQ(model.planes.size(), 1u);
    EXPECT_NEAR(model.planes.at(plane).frame.Location().Y(), -15.0, 1e-9);
    EXPECT_NEAR(totalVolume(model), 8000.0, 1e-3);
    EXPECT_TRUE(geometry::findFace(model.bodies[0].shape, gp_Pnt(20, -25, 10), 1e-6).has_value());

    std::get<PlaneFeature>(doc.find(plane)->data).offset = 30.0;
    model = recompute(doc);
    EXPECT_TRUE(geometry::findFace(model.bodies[0].shape, gp_Pnt(20, -40, 10), 1e-6).has_value());

    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
    EXPECT_TRUE(geometry::findFace(recompute(fromJson(text, shapes)).bodies[0].shape, gp_Pnt(20, -40, 10), 1e-6));

    doc.remove(plane);
    model = recompute(doc);
    EXPECT_EQ(model.status.at(base).health, FeatureHealth::Failed);
}

TEST(Recompute, OffsetPlaneFromFace)
{
    PlaneFeature offset;
    offset.base = gp_Ax3(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    offset.baseName = "Face";
    offset.center = gp_Pnt(20, 10, 10);
    offset.offset = -4.0;
    const ConstructionPlane placed = constructionPlane(offset);
    EXPECT_NEAR(placed.frame.Location().Z(), 6.0, 1e-9);
    EXPECT_TRUE(placed.frame.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-12));
    EXPECT_TRUE(placed.frame.XDirection().IsEqual(gp_Dir(1, 0, 0), 1e-12));
    EXPECT_TRUE(placed.center.IsEqual(gp_Pnt(20, 10, 6), 1e-9));

    Document doc;
    doc.add(PlaneFeature{});
    EXPECT_EQ(recompute(doc).status.at(doc.features().front().id).health, FeatureHealth::Failed);
}

TEST(Serialization, ShellRoundTrips)
{
    Document doc;
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::NewBody);
    doc.add(ShellFeature{{gp_Pnt(10, 20, 10), gp_Pnt(10, 0, 5)}, 1.5, false});
    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
}

TEST(Serialization, ImportedBodySurvivesSaveAndLoad)
{
    const kalve137::geometry::Color red{0.8, 0.1, 0.1};
    Document doc;
    const std::vector<std::string> path{"Rig", "Arm"};
    doc.add(ImportFeature{"block.step", {{"Block", kalve137::geometry::makeBox(10.0, 20.0, 30.0), red, {}, path}}});
    const FeatureId base = addBaseSketch(doc);
    addExtrude(doc, base, {5, 5}, 10.0, Operation::Cut);

    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_EQ(model.bodies.front().name, "Block");
    EXPECT_EQ(model.bodies.front().color, red);
    const double expected = 6000.0 - 10.0 * 20.0 * 10.0;
    EXPECT_NEAR(totalVolume(model), expected, 1e-3);

    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
    const Model reloaded = recompute(fromJson(text, shapes));
    EXPECT_NEAR(totalVolume(reloaded), expected, 1e-3);
    EXPECT_EQ(reloaded.bodies.front().color, red);
    EXPECT_EQ(reloaded.bodies.front().path, path);
    EXPECT_EQ(reloaded.stepParts().front().path, path);
}

TEST(Serialization, ImportMakesOneBodyPerPart)
{
    Document doc;
    doc.add(
        ImportFeature{"pair.step",
                      {{"Box", kalve137::geometry::makeBox(10.0, 10.0, 10.0), std::nullopt},
                       {"Pin", kalve137::geometry::makeCylinder(gp_Pnt(30.0, 0.0, 0.0), 2.0, 10.0), std::nullopt}}});
    const Model model = recompute(doc);
    ASSERT_EQ(model.bodies.size(), 2u);
    EXPECT_EQ(model.bodies[0].name, "Box");
    EXPECT_EQ(model.bodies[1].name, "Pin");
    EXPECT_FALSE(model.bodies[1].color);
    EXPECT_EQ(model.body({1, 1}), &model.bodies[1]);
}

TEST(Serialization, PatternCopiesOnlyTheChosenImportedPart)
{
    Document doc;
    const FeatureId import = doc.add(
        ImportFeature{"pair.step",
                      {{"Box", kalve137::geometry::makeBox(10.0, 10.0, 10.0), std::nullopt},
                       {"Pin", kalve137::geometry::makeCylinder(gp_Pnt(30.0, 0.0, 0.0), 2.0, 10.0), std::nullopt}}});
    PatternFeature pattern;
    pattern.body = {import, 1};
    pattern.axis = {AxisKind::Y, 0, 0};
    pattern.count = 3;
    pattern.spacing = 10.0;
    doc.add(pattern);

    const double pin = std::numbers::pi * 4.0 * 10.0;
    const double expected = 1000.0 + 3.0 * pin;
    EXPECT_NEAR(totalVolume(recompute(doc)), expected, 1e-3);
    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
    EXPECT_NEAR(totalVolume(recompute(fromJson(text, shapes))), expected, 1e-3);
}

TEST(Serialization, VersionOneImportLoadsAsOnePart)
{
    using nlohmann::json;
    const json data = {{"source", "block.step"},
                       {"body_name", "Block"},
                       {"brep", kalve137::geometry::toBrep(kalve137::geometry::makeBox(10.0, 20.0, 30.0))}};
    const json feature = {{"id", 1}, {"type", "import"}, {"name", "Import1"}, {"suppressed", false}, {"data", data}};
    const json legacy = {{"format", "kalve137-design"},
                         {"version", 1},
                         {"marker", 1},
                         {"next_id", 2},
                         {"features", json::array({feature})}};

    ShapeStore shapes;
    const Model model = recompute(fromJson(legacy.dump(), shapes));
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_EQ(model.bodies.front().name, "Block");
    EXPECT_FALSE(model.bodies.front().color);
    EXPECT_NEAR(totalVolume(model), 6000.0, 1e-3);
}

TEST(Serialization, RoundTripRebuildsSameModel)
{
    const Document doc = blockWithHole();
    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    EXPECT_NEAR(totalVolume(recompute(loaded)), kBlockWithHoleVolume, 1e-3);
}

TEST(Serialization, DimensionLabelsRoundTrip)
{
    Document doc = blockWithHole();
    sketch::Sketch& s = std::get<SketchFeature>(doc.find(doc.features().front().id)->data).sketch;
    const sketch::Id line = s.curves().begin()->first;
    const sketch::Id placed = s.addConstraint(sketch::ConstraintKind::Distance, {line}, 1.0);
    s.constraint(placed)->labelOffset = sketch::Vec2{2.5, -7.0};
    const sketch::Id automatic = s.addConstraint(sketch::ConstraintKind::Distance, {line}, 1.0);
    ShapeStore shapes;
    const Document loaded = fromJson(toJson(doc, shapes), shapes);
    const sketch::Sketch& read = std::get<SketchFeature>(loaded.find(loaded.features().front().id)->data).sketch;
    ASSERT_TRUE(read.constraint(placed)->labelOffset);
    EXPECT_DOUBLE_EQ(read.constraint(placed)->labelOffset->x, 2.5);
    EXPECT_DOUBLE_EQ(read.constraint(placed)->labelOffset->y, -7.0);
    EXPECT_FALSE(read.constraint(automatic)->labelOffset);
}

TEST(Serialization, RejectsOtherFiles)
{
    ShapeStore shapes;
    EXPECT_THROW(fromJson("{}", shapes), std::runtime_error);
    EXPECT_THROW(fromJson("not json", shapes), std::runtime_error);
    EXPECT_THROW(fromJson(R"({"format":"kalve137-design","version":99,"features":[]})", shapes), std::runtime_error);
}

TEST(Serialization, RejectsDamagedSketches)
{
    ShapeStore shapes;
    const nlohmann::json good = nlohmann::json::parse(toJson(blockWithHole(), shapes));
    const auto rejects = [&](const char* what, const auto& damage) {
        nlohmann::json design = good;
        damage(design["features"][0]["data"]["sketch"]);
        try {
            fromJson(design.dump(), shapes);
            ADD_FAILURE() << what << " loaded";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("damaged"), std::string::npos) << what << ": " << error.what();
        }
    };
    rejects("missing point", [](nlohmann::json& s) { s["curves"][0]["points"][1] = 999; });
    rejects("too few points", [](nlohmann::json& s) { s["curves"][0]["points"].erase(1); });
    rejects("non-finite position", [](nlohmann::json& s) { s["points"][1]["x"] = nullptr; });
    rejects("dead constraint reference", [](nlohmann::json& s) {
        s["constraints"].push_back(
            {{"id", 900}, {"kind", "horizontal"}, {"refs", nlohmann::json::array({999})}, {"value", 0.0}});
    });
    rejects("shared id", [](nlohmann::json& s) {
        s["constraints"].push_back({{"id", s["curves"][0]["id"]},
                                    {"kind", "horizontal"},
                                    {"refs", nlohmann::json::array({s["curves"][1]["id"]})},
                                    {"value", 0.0}});
    });
}

TEST(Serialization, RejectsDuplicateFeatureIds)
{
    ShapeStore shapes;
    nlohmann::json design = nlohmann::json::parse(toJson(blockWithHole(), shapes));
    design["features"][1]["id"] = design["features"][0]["id"];
    EXPECT_THROW(fromJson(design.dump(), shapes), std::runtime_error);
}

TEST(Serialization, RejectsOversizedPatterns)
{
    Document doc = blockWithHole();
    PatternFeature pattern;
    pattern.body = {doc.features()[1].id, 0};
    pattern.count = kMaxPatternCount;
    doc.add(pattern);
    ShapeStore shapes;
    nlohmann::json design = nlohmann::json::parse(toJson(doc, shapes));
    EXPECT_NO_THROW(fromJson(design.dump(), shapes));
    design["features"].back()["data"]["count"] = 2000000000;
    EXPECT_THROW(fromJson(design.dump(), shapes), std::runtime_error);
}

TEST(Recompute, FailedFeatureLeavesEveryBodyUnchanged)
{
    Document doc;
    const FeatureId tall = addBaseSketch(doc);
    addExtrude(doc, tall, {5, 5}, 10.0, Operation::NewBody);
    SketchFeature thin;
    sketch::Sketch& s = thin.sketch;
    const auto a = s.addPoint({100, 0});
    const auto b = s.addPoint({140, 0});
    const auto c = s.addPoint({140, 20});
    const auto d = s.addPoint({100, 20});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
    const FeatureId thinSketch = doc.add(std::move(thin));
    addExtrude(doc, thinSketch, {105, 5}, 2.0, Operation::NewBody);
    const Model before = recompute(doc);
    ASSERT_EQ(before.bodies.size(), 2u);

    const FeatureId fillet = doc.add(FilletFeature{{FilletSet{{gp_Pnt(20, 0, 10), gp_Pnt(120, 0, 2)}, 3.0}}});
    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(fillet).health, FeatureHealth::Failed);
    ASSERT_EQ(model.bodies.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_NEAR(geometry::volume(model.bodies[i].shape), geometry::volume(before.bodies[i].shape), 1e-6);
    }
}

TEST(UndoStack, UndoAndRedoRestoreSnapshots)
{
    Document doc;
    UndoStack undo;
    undo.push("Sketch", doc);
    const FeatureId sketch = addBaseSketch(doc);
    undo.push("Extrude", doc);
    addExtrude(doc, sketch, {5, 5}, 10.0, Operation::NewBody);

    undo.undo(doc);
    EXPECT_EQ(doc.features().size(), 1u);
    EXPECT_EQ(undo.redoLabel(), "Extrude");
    undo.redo(doc);
    EXPECT_EQ(doc.features().size(), 2u);
    undo.undo(doc);
    undo.undo(doc);
    EXPECT_TRUE(doc.features().empty());
    EXPECT_FALSE(undo.canUndo());
}

TEST(Project, CreateSaveAndList)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-test-project";
    std::filesystem::remove_all(folder);
    const Project project = Project::create(folder, "Bracket");
    EXPECT_EQ(project.name(), "Bracket");
    EXPECT_TRUE(std::filesystem::is_directory(project.exportsFolder()));

    const auto path = project.freeDesignPath("Bracket");
    ShapeStore shapes(shapeFolder(path));
    saveDesign(blockWithHole(), path, shapes);
    EXPECT_EQ(project.freeDesignPath("Bracket").filename(), "Bracket 2.k137");
    ASSERT_EQ(project.designs().size(), 1u);

    const Project reopened = Project::open(folder / kProjectFileName);
    EXPECT_EQ(reopened.designs().front(), path);
    EXPECT_NEAR(totalVolume(recompute(loadDesign(path, shapes))), kBlockWithHoleVolume, 1e-3);
    std::filesystem::remove_all(folder);
}
