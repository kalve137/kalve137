// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/naming.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "core/undo_stack.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Bnd_Box.hxx>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kalve137;
using namespace kalve137::core;

namespace {

using Step = std::function<void(Document&)>;

const std::filesystem::path kFixtures = std::filesystem::path(KALVE137_SOURCE_DIR) / "tests" / "core" / "fixtures";

SketchFeature rectangle(double x0, double y0, double x1, double y1, OriginPlane plane = OriginPlane::XY)
{
    SketchFeature data;
    data.plane = originPlane(plane);
    data.planeName = std::string(toString(plane));
    sketch::Sketch& s = data.sketch;
    const auto a = s.addPoint({x0, y0});
    const auto b = s.addPoint({x1, y0});
    const auto c = s.addPoint({x1, y1});
    const auto d = s.addPoint({x0, y1});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
    return data;
}

FeatureId lastId(const Document& doc)
{
    return doc.features().back().id;
}

FeatureId idAt(const Document& doc, std::size_t index)
{
    return doc.features().at(index).id;
}

void extrudeLast(Document& doc, sketch::Vec2 seed, double distance)
{
    ExtrudeFeature data;
    data.profiles = {lastId(doc), {seed}};
    data.distance = distance;
    data.bodyName = doc.takeBodyName();
    doc.add(std::move(data));
}

std::vector<Step> everyKindSteps()
{
    std::vector<Step> steps;
    steps.emplace_back([](Document& doc) { doc.add(rectangle(0, 0, 40, 20)); });
    steps.emplace_back([](Document& doc) { extrudeLast(doc, {5, 5}, 10.0); });
    steps.emplace_back([](Document& doc) { doc.add(FilletFeature{{FilletSet{{gp_Pnt(20, 0, 10)}, 2.0}}}); });
    steps.emplace_back([](Document& doc) { doc.add(ChamferFeature{{gp_Pnt(20, 20, 10)}, 1.0}); });
    steps.emplace_back([](Document& doc) {
        HoleFeature hole;
        hole.facePoints = {gp_Pnt(30, 10, 10)};
        hole.diameter = 4.0;
        hole.depth = 5.0;
        doc.add(hole);
    });
    steps.emplace_back([](Document& doc) { doc.add(OffsetFaceFeature{{gp_Pnt(0, 10, 5)}, 2.0}); });
    steps.emplace_back([](Document& doc) { doc.add(rectangle(60, 0, 80, 20)); });
    steps.emplace_back([](Document& doc) { extrudeLast(doc, {65, 5}, 10.0); });
    steps.emplace_back([](Document& doc) { doc.add(ShellFeature{{gp_Pnt(70, 10, 10)}, 1.0, true}); });
    steps.emplace_back([](Document& doc) {
        PatternFeature pattern;
        pattern.body = {idAt(doc, 7), 0};
        pattern.axis = {AxisKind::Y, 0, 0};
        pattern.count = 2;
        pattern.spacing = 30.0;
        doc.add(pattern);
    });
    steps.emplace_back([](Document& doc) { doc.add(rectangle(100, 0, 110, 10, OriginPlane::XZ)); });
    steps.emplace_back([](Document& doc) {
        RevolveFeature revolve;
        revolve.profiles = {lastId(doc), {{105, 5}}};
        revolve.axis = {AxisKind::Z, 0, 0};
        revolve.angleDegrees = 90.0;
        revolve.bodyName = doc.takeBodyName();
        doc.add(std::move(revolve));
    });
    steps.emplace_back([](Document& doc) {
        MoveFeature move;
        move.bodies = {{lastId(doc), 0}};
        move.translation = gp_Vec(0, 0, 20);
        doc.add(move);
    });
    steps.emplace_back([](Document& doc) {
        AppearanceFeature appearance;
        appearance.bodies = {{idAt(doc, 1), 0}};
        appearance.color = geometry::Color{0.8, 0.6, 0.2};
        doc.add(appearance);
    });
    steps.emplace_back([](Document& doc) {
        PlaneFeature plane;
        plane.baseName = "XY";
        plane.offset = 30.0;
        doc.add(plane);
    });
    steps.emplace_back([](Document& doc) {
        PlaneFeature plane;
        plane.kind = PlaneKind::Angle;
        plane.baseName = "XY";
        plane.hinge = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        plane.angleDegrees = 30.0;
        doc.add(plane);
    });
    steps.emplace_back([](Document& doc) {
        PlaneFeature plane;
        plane.kind = PlaneKind::Midplane;
        plane.baseName = "XY";
        plane.second = gp_Ax3(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
        plane.secondName = "Face";
        plane.secondCenter = gp_Pnt(0, 0, 10);
        doc.add(plane);
    });
    steps.emplace_back([](Document& doc) {
        doc.add(AxisFeature{AxisSource::TwoPoints, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), "Points"});
    });
    steps.emplace_back([](Document& doc) { doc.add(PointFeature{gp_Pnt(1, 2, 3), "Point"}); });
    return steps;
}

Document everyKind()
{
    Document doc;
    for (const Step& step : everyKindSteps()) {
        step(doc);
    }
    return doc;
}

std::vector<double> volumes(const Model& model)
{
    std::vector<double> result;
    for (const Body& body : model.bodies) {
        result.push_back(geometry::volume(body.shape));
    }
    return result;
}

void expectSameVolumes(const Model& actual, const Model& expected)
{
    ASSERT_EQ(actual.bodies.size(), expected.bodies.size());
    for (std::size_t i = 0; i < actual.bodies.size(); ++i) {
        EXPECT_EQ(actual.bodies[i].name, expected.bodies[i].name);
        EXPECT_NEAR(geometry::volume(actual.bodies[i].shape), geometry::volume(expected.bodies[i].shape), 1e-6);
    }
}

void expectSound(const Model& model)
{
    for (const auto& [id, status] : model.status) {
        if (status.health == FeatureHealth::Failed || status.health == FeatureHealth::Warning) {
            EXPECT_FALSE(status.message.empty()) << "feature " << id;
        }
    }
    for (const Body& body : model.bodies) {
        EXPECT_FALSE(body.shape.IsNull()) << body.name;
        if (body.shape.IsNull()) {
            continue;
        }
        EXPECT_TRUE(BRepCheck_Analyzer(body.shape).IsValid()) << body.name;
        const double v = geometry::volume(body.shape);
        EXPECT_TRUE(std::isfinite(v) && v > 0.0) << body.name << " volume " << v;
    }
}

template <typename Data>
void sweep(const char* what, const std::vector<double>& values, const std::function<void(Data&, double)>& set)
{
    const Document base = everyKind();
    for (std::size_t index = 0; index < base.features().size(); ++index) {
        if (!std::holds_alternative<Data>(base.features()[index].data)) {
            continue;
        }
        for (const double value : values) {
            SCOPED_TRACE(std::string(what) + " " + base.features()[index].name + " = " + std::to_string(value));
            Document doc = base;
            Feature* feature = doc.find(idAt(doc, index));
            set(std::get<Data>(feature->data), value);
            const Model model = recompute(doc);
            expectSound(model);
        }
    }
}

} // namespace

TEST(Sweep, EveryKindRecomputesCleanly)
{
    const Document doc = everyKind();
    const Model model = recompute(doc);
    for (const Feature& feature : doc.features()) {
        EXPECT_EQ(model.status.at(feature.id).health, FeatureHealth::Ok)
            << feature.name << ": " << model.status.at(feature.id).message;
    }
    expectSound(model);
    EXPECT_EQ(model.bodies.size(), 3u);
}

TEST(Sweep, EveryKindRoundTripsThroughJson)
{
    const Document doc = everyKind();
    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    expectSameVolumes(recompute(loaded), recompute(doc));
}

TEST(Sweep, EveryKindSurvivesSaveAndLoad)
{
    const auto folder = std::filesystem::temp_directory_path() / "kalve137-sweep";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto path = folder / "every-kind.k137";
    const Document doc = everyKind();
    {
        ShapeStore shapes(shapeFolder(path));
        saveDesign(doc, path, shapes);
    }
    ShapeStore shapes(shapeFolder(path));
    expectSameVolumes(recompute(loadDesign(path, shapes)), recompute(doc));
    std::filesystem::remove_all(folder);
}

TEST(Sweep, RemovingAnyFeatureKeepsTheRestSound)
{
    const Document base = everyKind();
    for (const Feature& feature : base.features()) {
        SCOPED_TRACE("without " + feature.name);
        Document doc = base;
        doc.remove(feature.id);
        Model model;
        ASSERT_NO_THROW(model = recompute(doc));
        expectSound(model);
        ShapeStore shapes;
        const std::string text = toJson(doc, shapes);
        EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
    }
}

TEST(Sweep, RollingBackToAnyMarkerMatchesAShorterHistory)
{
    const std::vector<Step> steps = everyKindSteps();
    const Document full = everyKind();
    Document prefix;
    for (std::size_t marker = 0; marker <= steps.size(); ++marker) {
        SCOPED_TRACE("marker " + std::to_string(marker));
        Document rolled = full;
        rolled.setMarker(marker);
        const Model model = recompute(rolled);
        expectSound(model);
        expectSameVolumes(model, recompute(prefix));
        if (marker < steps.size()) {
            steps[marker](prefix);
        }
    }
}

TEST(Sweep, UndoToEmptyAndRedoToTheEndRestoresEveryStep)
{
    ShapeStore shapes;
    Document doc;
    UndoStack undo;
    std::vector<std::string> states{toJson(doc, shapes)};
    for (const Step& step : everyKindSteps()) {
        undo.push("Step", doc);
        step(doc);
        states.push_back(toJson(doc, shapes));
    }
    for (std::size_t i = states.size() - 1; i > 0; --i) {
        ASSERT_TRUE(undo.canUndo());
        undo.undo(doc);
        EXPECT_EQ(toJson(doc, shapes), states[i - 1]) << "undo to " << i - 1;
    }
    EXPECT_FALSE(undo.canUndo());
    for (std::size_t i = 1; i < states.size(); ++i) {
        ASSERT_TRUE(undo.canRedo());
        undo.redo(doc);
        EXPECT_EQ(toJson(doc, shapes), states[i]) << "redo to " << i;
    }
    EXPECT_FALSE(undo.canRedo());
    expectSameVolumes(recompute(doc), recompute(everyKind()));
}

TEST(Sweep, ExtrudeDistances)
{
    sweep<ExtrudeFeature>("distance", {-50.0, -1.0, 0.0, 1e-4, 0.01, 1.0, 500.0, 1e6},
                          [](ExtrudeFeature& data, double value) { data.distance = value; });
}

TEST(Sweep, FilletRadii)
{
    sweep<FilletFeature>("radius", {-1.0, 0.0, 0.01, 1.0, 4.99, 5.0, 9.99, 10.0, 20.0, 100.0},
                         [](FilletFeature& data, double value) { data.sets.front().radius = value; });
}

TEST(Sweep, ChamferDistances)
{
    sweep<ChamferFeature>("distance", {-1.0, 0.0, 0.01, 1.0, 4.99, 5.0, 9.99, 10.0, 20.0, 100.0},
                          [](ChamferFeature& data, double value) { data.distance = value; });
}

TEST(Sweep, ShellThicknesses)
{
    sweep<ShellFeature>("thickness", {-1.0, 0.0, 0.01, 1.0, 4.9, 5.0, 9.9, 10.0, 50.0},
                        [](ShellFeature& data, double value) { data.thickness = value; });
}

TEST(Sweep, HoleSizes)
{
    sweep<HoleFeature>("diameter", {-1.0, 0.0, 0.01, 1.0, 15.9, 16.0, 40.0, 1000.0},
                       [](HoleFeature& data, double value) { data.diameter = value; });
    sweep<HoleFeature>("depth", {-1.0, 0.0, 0.01, 10.0, 10.01, 100.0},
                       [](HoleFeature& data, double value) { data.depth = value; });
}

TEST(Sweep, OffsetFaceDistances)
{
    sweep<OffsetFaceFeature>("distance", {-100.0, -40.0, -39.99, -5.0, 0.0, 5.0, 1000.0},
                             [](OffsetFaceFeature& data, double value) { data.distance = value; });
}

TEST(Sweep, PatternSpacings)
{
    sweep<PatternFeature>("spacing", {-30.0, -1.0, 0.0, 1.0, 19.99, 20.0, 1000.0},
                          [](PatternFeature& data, double value) { data.spacing = value; });
    sweep<PatternFeature>("count", {kMinPatternCount, 10.0, kMaxPatternCount},
                          [](PatternFeature& data, double value) { data.count = static_cast<int>(value); });
}

TEST(Sweep, RevolveAngles)
{
    sweep<RevolveFeature>("angle", {-360.0, -90.0, 0.0, 0.01, 180.0, 359.99, 360.0, 720.0},
                          [](RevolveFeature& data, double value) { data.angleDegrees = value; });
}

TEST(Sweep, PlaneOffsetsAndAngles)
{
    sweep<PlaneFeature>("offset", {-1e6, -30.0, 0.0, 30.0, 1e6},
                        [](PlaneFeature& data, double value) { data.offset = value; });
    sweep<PlaneFeature>("angle", {-360.0, -90.0, 0.0, 90.0, 180.0, 360.0},
                        [](PlaneFeature& data, double value) { data.angleDegrees = value; });
}

namespace {

struct FaceSketchDesign {
    Document doc;
    FeatureId base = 0;
    FeatureId sketch = 0;
};

FaceSketchDesign sketchOnTopFace()
{
    FaceSketchDesign design;
    Document& doc = design.doc;
    doc.add(rectangle(0, 0, 40, 20));
    extrudeLast(doc, {5, 5}, 10.0);
    design.base = lastId(doc);
    const Model model = recompute(doc);
    const Body& body = model.bodies.front();
    const gp_Pnt onTop(20, 10, 10);
    const auto top = geometry::findFace(body.shape, onTop, 1e-6);
    EXPECT_TRUE(top.has_value());

    SketchFeature data = rectangle(5, 5, 15, 15);
    data.plane = *geometry::planarFrame(*top);
    data.planeName = "Face";
    data.face = faceRef(body, *top, onTop);
    design.sketch = doc.add(std::move(data));
    extrudeLast(doc, {10, 10}, 5.0);
    return design;
}

double topOf(const Model& model, std::size_t body)
{
    Bnd_Box box;
    BRepBndLib::Add(model.bodies.at(body).shape, box);
    return box.CornerMax().Z();
}

} // namespace

TEST(SketchOnFace, FollowsTheFaceWhenTheBaseGrows)
{
    FaceSketchDesign design = sketchOnTopFace();
    Model model = recompute(design.doc);
    ASSERT_EQ(model.bodies.size(), 2u);
    EXPECT_NEAR(topOf(model, 1), 15.0, 1e-6);

    std::get<ExtrudeFeature>(design.doc.find(design.base)->data).distance = 25.0;
    model = recompute(design.doc);
    EXPECT_EQ(model.status.at(design.sketch).health, FeatureHealth::Ok) << model.status.at(design.sketch).message;
    EXPECT_NEAR(model.sketches.at(design.sketch).plane.Location().Z(), 25.0, 1e-6);
    EXPECT_NEAR(topOf(model, 1), 30.0, 1e-6);
    EXPECT_NEAR(geometry::volume(model.bodies[1].shape), 10.0 * 10.0 * 5.0, 1e-6);
}

TEST(SketchOnFace, KeepsItsLastPlaceWhenTheFaceIsGone)
{
    FaceSketchDesign design = sketchOnTopFace();
    design.doc.find(design.base)->suppressed = true;
    const Model model = recompute(design.doc);
    EXPECT_EQ(model.status.at(design.sketch).health, FeatureHealth::Warning);
    EXPECT_NEAR(model.sketches.at(design.sketch).plane.Location().Z(), 10.0, 1e-6);
    ASSERT_EQ(model.bodies.size(), 1u);
    EXPECT_NEAR(topOf(model, 0), 15.0, 1e-6);
}

TEST(SketchOnFace, RoundTripsTheFaceReference)
{
    const FaceSketchDesign design = sketchOnTopFace();
    ShapeStore shapes;
    const std::string text = toJson(design.doc, shapes);
    EXPECT_EQ(nlohmann::json::parse(text)["version"], kDesignFormatVersion);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    const auto& sketch = std::get<SketchFeature>(loaded.find(design.sketch)->data);
    ASSERT_TRUE(sketch.face.has_value());
    EXPECT_FALSE(sketch.face->names.empty());
}

TEST(FilletSets, EachSetUsesItsOwnRadius)
{
    Document doc;
    doc.add(rectangle(0, 0, 40, 20));
    extrudeLast(doc, {5, 5}, 10.0);
    FilletFeature fillet;
    fillet.sets = {FilletSet{{gp_Pnt(20, 0, 10)}, 2.0}, FilletSet{{gp_Pnt(20, 20, 0)}, 3.0}};
    const FeatureId id = doc.add(fillet);
    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
    const double corner = 1.0 - std::numbers::pi / 4.0;
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 8000.0 - corner * 40.0 * (4.0 + 9.0), 1e-3);

    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    EXPECT_EQ(nlohmann::json::parse(text)["features"].back()["data"]["sets"].size(), 2u);
    EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
}

TEST(FilletSets, AnEdgeInTwoSetsIsFilletedOnce)
{
    Document doc;
    doc.add(rectangle(0, 0, 40, 20));
    extrudeLast(doc, {5, 5}, 10.0);
    FilletFeature fillet;
    fillet.sets = {FilletSet{{gp_Pnt(20, 0, 10)}, 2.0}, FilletSet{{gp_Pnt(20, 0, 10)}, 3.0}};
    const FeatureId id = doc.add(fillet);
    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 8000.0 - (4.0 - std::numbers::pi) * 40.0, 1e-3);
}

TEST(FilletSets, RejectsAFileWithNoSets)
{
    ShapeStore shapes;
    nlohmann::json design = nlohmann::json::parse(toJson(everyKind(), shapes));
    for (nlohmann::json& feature : design["features"]) {
        if (feature["type"] == "fillet") {
            feature["data"]["sets"] = nlohmann::json::array();
        }
    }
    EXPECT_THROW(fromJson(design.dump(), shapes), std::runtime_error);
}

namespace {

FeatureId extrudeWith(Document& doc, sketch::Vec2 seed, const std::function<void(ExtrudeFeature&)>& set)
{
    ExtrudeFeature data;
    data.profiles = {lastId(doc), {seed}};
    data.bodyName = doc.takeBodyName();
    set(data);
    return doc.add(std::move(data));
}

double frustum(double side, double height, double taperDegrees)
{
    const double top = side - 2.0 * height * std::tan(taperDegrees * std::numbers::pi / 180.0);
    return height / 3.0 * (side * side + top * top + side * top);
}

Document block()
{
    Document doc;
    doc.add(rectangle(0, 0, 40, 20));
    extrudeLast(doc, {5, 5}, 10.0);
    return doc;
}

} // namespace

TEST(ExtrudeEnds, AllCutsThroughWhateverTheBodyBecomes)
{
    Document doc = block();
    const FeatureId base = lastId(doc);
    doc.add(rectangle(10, 5, 20, 15));
    const FeatureId cut = extrudeWith(doc, {15, 10}, [](ExtrudeFeature& data) {
        data.distance = 1.0;
        data.end = ExtrudeEnd::All;
        data.operation = Operation::Cut;
    });
    Model model = recompute(doc);
    ASSERT_EQ(model.status.at(cut).health, FeatureHealth::Ok) << model.status.at(cut).message;
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 8000.0 - 1000.0, 1e-3);

    std::get<ExtrudeFeature>(doc.find(base)->data).distance = 25.0;
    model = recompute(doc);
    ASSERT_EQ(model.status.at(cut).health, FeatureHealth::Ok) << model.status.at(cut).message;
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 40.0 * 20.0 * 25.0 - 100.0 * 25.0, 1e-3);
}

TEST(ExtrudeEnds, AllBothWaysCutsBodiesOnEitherSide)
{
    Document doc = block();
    PlaneFeature mid;
    mid.baseName = "XY";
    mid.offset = 5.0;
    const FeatureId plane = doc.add(mid);
    SketchFeature sketch = rectangle(10, 5, 20, 15);
    sketch.planeFeature = plane;
    doc.add(std::move(sketch));
    const FeatureId cut = extrudeWith(doc, {15, 10}, [](ExtrudeFeature& data) {
        data.end = ExtrudeEnd::All;
        data.extent = ExtrudeExtent::Symmetric;
        data.operation = Operation::Cut;
    });
    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(cut).health, FeatureHealth::Ok) << model.status.at(cut).message;
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 8000.0 - 1000.0, 1e-3);
}

TEST(ExtrudeEnds, ToObjectStopsAtAPlaneOrAFlatFace)
{
    Document doc = block();
    PlaneFeature high;
    high.baseName = "XY";
    high.offset = 30.0;
    const FeatureId plane = doc.add(high);
    doc.add(rectangle(50, 0, 60, 10));
    const FeatureId toPlane = extrudeWith(doc, {55, 5}, [&](ExtrudeFeature& data) {
        data.end = ExtrudeEnd::ToObject;
        data.toPlane = plane;
    });
    Model model = recompute(doc);
    ASSERT_EQ(model.status.at(toPlane).health, FeatureHealth::Ok) << model.status.at(toPlane).message;
    EXPECT_NEAR(geometry::volume(model.bodies.at(1).shape), 100.0 * 30.0, 1e-3);

    const Body& base = model.bodies.front();
    const gp_Pnt onTop(20, 10, 10);
    const auto top = geometry::findFace(base.shape, onTop, 1e-6);
    ASSERT_TRUE(top.has_value());
    const TopoRef topRef = faceRef(base, *top, onTop);
    doc.add(rectangle(70, 0, 80, 10));
    const FeatureId toFace = extrudeWith(doc, {75, 5}, [&](ExtrudeFeature& data) {
        data.end = ExtrudeEnd::ToObject;
        data.toFace = topRef;
    });
    model = recompute(doc);
    ASSERT_EQ(model.status.at(toFace).health, FeatureHealth::Ok) << model.status.at(toFace).message;
    EXPECT_NEAR(geometry::volume(model.bodies.at(2).shape), 100.0 * 10.0, 1e-3);

    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    const auto& data = std::get<ExtrudeFeature>(loaded.find(toFace)->data);
    EXPECT_EQ(data.end, ExtrudeEnd::ToObject);
    ASSERT_TRUE(data.toFace.has_value());
    EXPECT_FALSE(data.toFace->names.empty());
}

TEST(ExtrudeEnds, ToObjectRejectsATiltedPlane)
{
    Document doc;
    PlaneFeature tilted;
    tilted.kind = PlaneKind::Angle;
    tilted.baseName = "XY";
    tilted.hinge = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
    tilted.angleDegrees = 30.0;
    const FeatureId plane = doc.add(tilted);
    doc.add(rectangle(0, 0, 10, 10));
    const FeatureId id = extrudeWith(doc, {5, 5}, [&](ExtrudeFeature& data) {
        data.end = ExtrudeEnd::ToObject;
        data.toPlane = plane;
    });
    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(id).health, FeatureHealth::Failed);
}

TEST(ExtrudeEnds, TaperNarrowsOrWidensTheExtrude)
{
    for (const double taper : {5.0, -5.0, 20.0}) {
        for (const double distance : {10.0, -10.0}) {
            Document doc;
            doc.add(rectangle(0, 0, 10, 10));
            const FeatureId id = extrudeWith(doc, {5, 5}, [&](ExtrudeFeature& data) {
                data.distance = distance;
                data.taperDegrees = taper;
            });
            const Model model = recompute(doc);
            ASSERT_EQ(model.status.at(id).health, FeatureHealth::Ok)
                << taper << " " << distance << ": " << model.status.at(id).message;
            EXPECT_NEAR(geometry::volume(model.bodies.front().shape), frustum(10.0, 10.0, taper), 1e-3)
                << taper << " " << distance;
            Bnd_Box box;
            BRepBndLib::Add(model.bodies.front().shape, box);
            EXPECT_NEAR(distance > 0.0 ? box.CornerMax().Z() : box.CornerMin().Z(), distance, 1e-3);
        }
    }
}

TEST(ExtrudeEnds, SymmetricTaperNarrowsBothWays)
{
    Document doc;
    doc.add(rectangle(0, 0, 10, 10));
    const FeatureId id = extrudeWith(doc, {5, 5}, [](ExtrudeFeature& data) {
        data.extent = ExtrudeExtent::Symmetric;
        data.taperDegrees = 10.0;
    });
    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape), 2.0 * frustum(10.0, 5.0, 10.0), 1e-3);
}

TEST(ExtrudeEnds, TaperedCircleBecomesACone)
{
    Document doc;
    SketchFeature circle;
    circle.plane = originPlane(OriginPlane::XY);
    circle.planeName = "XY";
    circle.sketch.addCircle(circle.sketch.addPoint({0, 0}), 5.0);
    doc.add(std::move(circle));
    const FeatureId id = extrudeWith(doc, {0, 0}, [](ExtrudeFeature& data) { data.taperDegrees = 10.0; });
    const Model model = recompute(doc);
    ASSERT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
    const double top = 5.0 - 10.0 * std::tan(10.0 * std::numbers::pi / 180.0);
    EXPECT_NEAR(geometry::volume(model.bodies.front().shape),
                std::numbers::pi * 10.0 / 3.0 * (25.0 + top * top + 5.0 * top), 1e-3);
}

TEST(ExtrudeEnds, TooSteepATaperIsAnError)
{
    Document doc;
    doc.add(rectangle(0, 0, 10, 10));
    const FeatureId id = extrudeWith(doc, {5, 5}, [](ExtrudeFeature& data) {
        data.distance = 50.0;
        data.taperDegrees = 30.0;
    });
    const Model model = recompute(doc);
    EXPECT_EQ(model.status.at(id).health, FeatureHealth::Failed);
}

TEST(ExtrudeEnds, TaperAndEndRoundTrip)
{
    Document doc;
    doc.add(rectangle(0, 0, 10, 10));
    extrudeWith(doc, {5, 5}, [](ExtrudeFeature& data) {
        data.taperDegrees = 3.5;
        data.end = ExtrudeEnd::All;
    });
    ShapeStore shapes;
    const std::string text = toJson(doc, shapes);
    const nlohmann::json saved = nlohmann::json::parse(text)["features"].back()["data"];
    EXPECT_EQ(saved["end"], "all");
    EXPECT_EQ(saved["taper"], 3.5);
    EXPECT_EQ(toJson(fromJson(text, shapes), shapes), text);
}

TEST(Sweep, VersionSixFixtureStillLoads)
{
    const auto fixture = kFixtures / "every-kind-v6.k137";
    ASSERT_TRUE(std::filesystem::exists(fixture)) << fixture;
    ShapeStore shapes;
    const std::string text = readFile(fixture);
    ASSERT_EQ(nlohmann::json::parse(text)["version"], 6);
    const Document loaded = fromJson(text, shapes);
    const Model model = recompute(loaded);
    for (const Feature& feature : loaded.features()) {
        EXPECT_EQ(model.status.at(feature.id).health, FeatureHealth::Ok) << feature.name;
    }
    expectSameVolumes(model, recompute(everyKind()));
}
