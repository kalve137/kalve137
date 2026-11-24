// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/naming.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "geometry/modeling.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Trsf.hxx>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <numbers>
#include <string>

using namespace kalve137;
using namespace kalve137::core;
using sketch::ConstraintKind;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kDepth = 30.0;
constexpr double kTolerance = 1e-2;

struct Size {
    double width = 60.0;
    double height = 10.0;
};

struct Block {
    Document doc;
    FeatureId sketch = 0;
    sketch::Id width = 0;
    FeatureId pad = 0;
};

Block block()
{
    Block b;
    SketchFeature base;
    sketch::Sketch& s = base.sketch;
    const auto p1 = s.addPoint({50, 0});
    const auto p2 = s.addPoint({50, 25});
    const auto p3 = s.addPoint({0, 25});
    const auto bottom = s.addLine(sketch::kOriginId, p1);
    const auto right = s.addLine(p1, p2);
    const auto top = s.addLine(p2, p3);
    const auto left = s.addLine(p3, sketch::kOriginId);
    s.addConstraint(ConstraintKind::Horizontal, {bottom});
    s.addConstraint(ConstraintKind::Horizontal, {top});
    s.addConstraint(ConstraintKind::Vertical, {right});
    s.addConstraint(ConstraintKind::Vertical, {left});
    b.width = s.addConstraint(ConstraintKind::Distance, {bottom}, Size{}.width);
    s.addConstraint(ConstraintKind::Distance, {left}, kDepth);
    b.sketch = b.doc.add(std::move(base));
    ExtrudeFeature pad;
    pad.profiles = {b.sketch, {{5, 5}}};
    pad.distance = Size{}.height;
    pad.bodyName = b.doc.takeBodyName();
    b.pad = b.doc.add(std::move(pad));
    return b;
}

void resize(Block& b, Size size)
{
    std::get<ExtrudeFeature>(b.doc.find(b.pad)->data).distance = size.height;
    std::get<SketchFeature>(b.doc.find(b.sketch)->data).sketch.constraint(b.width)->value = size.width;
}

FeatureId addRectangle(Document& doc, double x0, double y0, double x1, double y1)
{
    SketchFeature data;
    sketch::Sketch& s = data.sketch;
    const auto a = s.addPoint({x0, y0});
    const auto b = s.addPoint({x1, y0});
    const auto c = s.addPoint({x1, y1});
    const auto d = s.addPoint({x0, y1});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
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

double area(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}

double box(Size size)
{
    return size.width * kDepth * size.height;
}

TopoRef edgeAt(const Model& model, const gp_Pnt& point)
{
    const Body& body = model.bodies.at(0);
    const std::optional<TopoDS_Edge> edge = geometry::findEdge(body.shape, point, 1e-6);
    if (!edge) {
        ADD_FAILURE() << "No edge at " << point.X() << " " << point.Y() << " " << point.Z();
        return point;
    }
    TopoRef ref = edgeRef(body, *edge, point);
    EXPECT_EQ(ref.names.size(), 2u);
    return ref;
}

TopoRef faceAt(const Model& model, const gp_Pnt& point)
{
    const Body& body = model.bodies.at(0);
    const std::optional<TopoDS_Face> face = geometry::findFace(body.shape, point, 1e-6);
    if (!face) {
        ADD_FAILURE() << "No face at " << point.X() << " " << point.Y() << " " << point.Z();
        return point;
    }
    TopoRef ref = faceRef(body, *face, point);
    EXPECT_FALSE(ref.names.empty());
    return ref;
}

TopoRef vertexAt(const Model& model, const gp_Pnt& point)
{
    const Body& body = model.bodies.at(0);
    for (TopExp_Explorer it(body.shape, TopAbs_VERTEX); it.More(); it.Next()) {
        const TopoDS_Vertex& vertex = TopoDS::Vertex(it.Current());
        if (BRep_Tool::Pnt(vertex).Distance(point) < 1e-6) {
            TopoRef ref = vertexRef(body, vertex);
            EXPECT_EQ(ref.names.size(), 3u);
            return ref;
        }
    }
    ADD_FAILURE() << "No vertex at " << point.X() << " " << point.Y() << " " << point.Z();
    return point;
}

void expectVertexAt(const Model& model, const TopoRef& ref, const gp_Pnt& expected)
{
    const std::optional<VertexHit> hit = resolveVertex(model.bodies, ref);
    ASSERT_TRUE(hit.has_value());
    const gp_Pnt at = BRep_Tool::Pnt(hit->vertex);
    EXPECT_LT(at.Distance(expected), 1e-6) << at.X() << " " << at.Y() << " " << at.Z();
}

void expectFollows(Block& b, FeatureId id, const std::function<double(Size)>& expected)
{
    for (const Size size : {Size{}, Size{60.0, 15.0}, Size{80.0, 10.0}}) {
        SCOPED_TRACE("width " + std::to_string(size.width) + " height " + std::to_string(size.height));
        resize(b, size);
        const Model model = recompute(b.doc);
        EXPECT_EQ(model.status.at(id).health, FeatureHealth::Ok) << model.status.at(id).message;
        EXPECT_NEAR(totalVolume(model), expected(size), kTolerance);
    }
    resize(b, Size{});
}

double roundOff(double radius, double length)
{
    return (1.0 - kPi / 4.0) * radius * radius * length;
}

std::optional<FaceHit> everyFaceAt(const std::vector<Body>& bodies, const gp_Pnt& point)
{
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(point);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        std::optional<FaceHit> best;
        double bestDistance = kRefTolerance;
        for (TopExp_Explorer it(bodies[i].shape, TopAbs_FACE); it.More(); it.Next()) {
            BRepExtrema_DistShapeShape distance(probe, it.Current());
            if (distance.IsDone() && distance.Value() <= bestDistance) {
                bestDistance = distance.Value();
                best = FaceHit{i, TopoDS::Face(it.Current())};
            }
        }
        if (best) {
            return best;
        }
    }
    return std::nullopt;
}

Model threeBodies()
{
    Document doc;
    addExtrude(doc, addRectangle(doc, 0, 0, 20, 10), {5, 5}, 8.0, Operation::NewBody);
    addExtrude(doc, addRectangle(doc, 40, 0, 60, 10), {45, 5}, 8.0, Operation::NewBody);
    SketchFeature round;
    round.sketch.addCircle(round.sketch.addPoint({80, 5}), 6.0);
    const FeatureId sketchId = doc.add(std::move(round));
    addExtrude(doc, sketchId, {80, 5}, 8.0, Operation::NewBody);
    return recompute(doc);
}

void expectSameHit(const std::optional<FaceHit>& actual, const std::optional<FaceHit>& expected)
{
    ASSERT_EQ(actual.has_value(), expected.has_value());
    if (expected) {
        EXPECT_EQ(actual->body, expected->body);
        EXPECT_TRUE(actual->face.IsSame(expected->face));
    }
}

} // namespace

TEST(Naming, FilletFollowsVerticalEdges)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const FeatureId id =
        b.doc.add(FilletFeature{{FilletSet{{edgeAt(base, gp_Pnt(0, 0, 5)), edgeAt(base, gp_Pnt(60, 0, 5))}, 3.0}}});
    expectFollows(b, id, [](Size s) { return box(s) - 2.0 * roundOff(3.0, s.height); });
}

TEST(Naming, FilletFollowsTopEdges)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const FeatureId id = b.doc.add(FilletFeature{{FilletSet{{edgeAt(base, gp_Pnt(30, 0, 10))}, 2.0}}});
    expectFollows(b, id, [](Size s) { return box(s) - roundOff(2.0, s.width); });

    Block right = block();
    const Model rightBase = recompute(right.doc);
    const FeatureId rightId = right.doc.add(FilletFeature{{FilletSet{{edgeAt(rightBase, gp_Pnt(60, 15, 10))}, 2.0}}});
    expectFollows(right, rightId, [](Size s) { return box(s) - roundOff(2.0, kDepth); });
}

TEST(Naming, FacesAndEdgesKnowTheFeatureThatMadeThem)
{
    Block b = block();
    const FeatureId fillet = b.doc.add(FilletFeature{{FilletSet{{edgeAt(recompute(b.doc), gp_Pnt(30, 0, 10))}, 2.0}}});
    const Model model = recompute(b.doc);
    const Body& body = model.bodies.at(0);
    const auto face = [&](const gp_Pnt& at) { return *geometry::findFace(body.shape, at, 1e-6); };
    const auto edge = [&](const gp_Pnt& at) { return *geometry::findEdge(body.shape, at, 1e-6); };

    EXPECT_EQ(featureOfFace(body, face(gp_Pnt(30, 15, 10))), b.pad);
    EXPECT_EQ(featureOfFace(body, face(gp_Pnt(30, 2.0 - std::numbers::sqrt2, 8.0 + std::numbers::sqrt2))), fillet);
    EXPECT_EQ(featureOfEdge(body, edge(gp_Pnt(30, 2, 10))), fillet);
    EXPECT_EQ(featureOfEdge(body, edge(gp_Pnt(60, 30, 5))), b.pad);
    EXPECT_EQ(featureOfName("F12/side/3"), 12);
    EXPECT_EQ(featureOfName("Fx"), 0);
    EXPECT_EQ(featureOfName(""), 0);
}

TEST(Naming, BodiesOfFindWhatEachFeatureShaped)
{
    Block b = block();
    const FeatureId fillet = b.doc.add(FilletFeature{{FilletSet{{edgeAt(recompute(b.doc), gp_Pnt(30, 0, 10))}, 2.0}}});
    const FeatureId other =
        addExtrude(b.doc, addRectangle(b.doc, 100, 0, 120, 20), {110, 10}, 10.0, Operation::NewBody);
    const FeatureId slot = addExtrude(b.doc, addRectangle(b.doc, 40, 5, 110, 15), {50, 10}, 10.0, Operation::Cut);
    const Model model = recompute(b.doc);
    ASSERT_EQ(model.status.at(slot).health, FeatureHealth::Ok) << model.status.at(slot).message;
    const BodyKey padBody{b.pad, 0};
    const BodyKey otherBody{other, 0};

    EXPECT_EQ(bodiesOf(model, *b.doc.find(b.pad)), std::vector{padBody});
    EXPECT_EQ(bodiesOf(model, *b.doc.find(fillet)), std::vector{padBody});
    EXPECT_EQ(bodiesOf(model, *b.doc.find(other)), std::vector{otherBody});
    EXPECT_EQ(bodiesOf(model, *b.doc.find(slot)), (std::vector{padBody, otherBody}));
    EXPECT_TRUE(bodiesOf(model, *b.doc.find(b.sketch)).empty());
}

TEST(Naming, ChamferFollowsTopEdge)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const FeatureId id = b.doc.add(ChamferFeature{{edgeAt(base, gp_Pnt(30, 0, 10))}, 2.0});
    expectFollows(b, id, [](Size s) { return box(s) - 2.0 * s.width; });
}

TEST(Naming, ShellFollowsTopFace)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const FeatureId id = b.doc.add(ShellFeature{{faceAt(base, gp_Pnt(30, 15, 10))}, 2.0, true});
    expectFollows(b, id, [](Size s) { return box(s) - (s.width - 4.0) * (kDepth - 4.0) * (s.height - 2.0); });
}

TEST(Naming, HoleFollowsTopFace)
{
    Block b = block();
    const Model base = recompute(b.doc);
    HoleFeature hole;
    hole.facePoints = {faceAt(base, gp_Pnt(30, 15, 10))};
    hole.diameter = 6.0;
    hole.throughAll = true;
    const FeatureId id = b.doc.add(hole);
    expectFollows(b, id, [](Size s) { return box(s) - kPi * 9.0 * s.height; });
}

TEST(Naming, PressPullFollowsTopFace)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const FeatureId id = b.doc.add(OffsetFaceFeature{{faceAt(base, gp_Pnt(30, 15, 10))}, 5.0});
    expectFollows(b, id, [](Size s) { return s.width * kDepth * (s.height + 5.0); });
}

TEST(Naming, AppearanceFollowsTopFace)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const geometry::Color blue{0, 0, 1};
    const FeatureId id = b.doc.add(AppearanceFeature{{}, {faceAt(base, gp_Pnt(30, 15, 10))}, blue});
    expectFollows(b, id, box);

    resize(b, Size{80.0, 15.0});
    const Model moved = recompute(b.doc);
    const Body& body = moved.bodies.at(0);
    const std::optional<TopoDS_Face> top = geometry::findFace(body.shape, gp_Pnt(70, 15, 15), 1e-6);
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(body.faceColors.find(*top), blue);
    EXPECT_EQ(body.faceColors.size(), 1u);
}

TEST(Naming, PatternAndBossKeepWorking)
{
    Block b = block();
    PatternFeature pattern;
    pattern.body = {b.pad, 0};
    pattern.count = 3;
    pattern.spacing = 100.0;
    const FeatureId patternId = b.doc.add(pattern);
    expectFollows(b, patternId, [](Size s) { return 3.0 * box(s); });

    Block boss = block();
    PlaneFeature plane;
    plane.baseName = "XY";
    plane.offset = 10.0;
    const FeatureId planeId = boss.doc.add(plane);
    SketchFeature top;
    top.planeFeature = planeId;
    top.planeName = "Plane1";
    top.sketch.addCircle(top.sketch.addPoint({30, 15}), 5.0);
    const FeatureId sketchId = boss.doc.add(std::move(top));
    const FeatureId bossId = addExtrude(boss.doc, sketchId, {30, 15}, 5.0, Operation::Join);
    expectFollows(boss, bossId, [](Size s) { return box(s) + (s.height < 15.0 ? kPi * 25.0 * 5.0 : 0.0); });
}

TEST(Naming, SplitFaceKeepsItsNameOnEveryPiece)
{
    Block b = block();
    const FeatureId groove = addRectangle(b.doc, 28, -5, 32, 35);
    addExtrude(b.doc, groove, {30, 15}, 3.0, Operation::Cut);
    const Model model = recompute(b.doc);
    const Body& body = model.bodies.at(0);

    const TopoRef left = faceAt(model, gp_Pnt(10, 15, 0));
    const TopoRef right = faceAt(model, gp_Pnt(50, 15, 0));
    EXPECT_EQ(left.names, right.names);
    EXPECT_EQ(left.names, std::vector<std::string>{namePrefix(b.pad) + "/start/p0"});

    const std::optional<FaceHit> leftHit = resolveFace(model.bodies, left);
    const std::optional<FaceHit> rightHit = resolveFace(model.bodies, right);
    ASSERT_TRUE(leftHit && rightHit);
    EXPECT_TRUE(leftHit->face.IsSame(*geometry::findFace(body.shape, gp_Pnt(10, 15, 0), 1e-6)));
    EXPECT_TRUE(rightHit->face.IsSame(*geometry::findFace(body.shape, gp_Pnt(50, 15, 0), 1e-6)));
    EXPECT_NEAR(area(rightHit->face), 28.0 * kDepth, kTolerance);

    resize(b, Size{80.0, 10.0});
    const Model wider = recompute(b.doc);
    const std::optional<FaceHit> widerHit = resolveFace(wider.bodies, right);
    ASSERT_TRUE(widerHit.has_value());
    EXPECT_NEAR(area(widerHit->face), 48.0 * kDepth, kTolerance);
}

TEST(Naming, MergedFaceCarriesBothNames)
{
    Block b = block();
    const Model before = recompute(b.doc);
    const TopoRef padTop = faceAt(before, gp_Pnt(30, 15, 10));

    const FeatureId side = addRectangle(b.doc, 60, 0, 80, kDepth);
    const FeatureId join = addExtrude(b.doc, side, {70, 15}, 10.0, Operation::Join);
    const Model model = recompute(b.doc);
    ASSERT_EQ(model.bodies.size(), 1u);

    const TopoRef merged = faceAt(model, gp_Pnt(70, 15, 10));
    EXPECT_EQ(merged.names.size(), 2u);
    EXPECT_NE(std::ranges::find(merged.names, padTop.names.front()), merged.names.end());
    EXPECT_NE(std::ranges::find(merged.names, namePrefix(join) + "/end/p0"), merged.names.end());

    const std::optional<FaceHit> hit = resolveFace(model.bodies, padTop);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(area(hit->face), 80.0 * kDepth, kTolerance);
}

TEST(Naming, VertexFollowsTheBoxCorner)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const TopoRef corner = vertexAt(base, gp_Pnt(60, 30, 10));
    const TopoRef origin = vertexAt(base, gp_Pnt(0, 0, 0));
    EXPECT_NE(corner.names, origin.names);
    expectVertexAt(base, corner, gp_Pnt(60, 30, 10));
    expectVertexAt(base, origin, gp_Pnt(0, 0, 0));

    resize(b, Size{80.0, 15.0});
    const Model moved = recompute(b.doc);
    expectVertexAt(moved, corner, gp_Pnt(80, 30, 15));
    expectVertexAt(moved, origin, gp_Pnt(0, 0, 0));

    expectVertexAt(moved, TopoRef{gp_Pnt(80, 30, 15.0005), {}}, gp_Pnt(80, 30, 15));
    EXPECT_FALSE(resolveVertex(moved.bodies, TopoRef{gp_Pnt(60, 30, 10), {}}).has_value());
    EXPECT_FALSE(resolveVertex(moved.bodies, TopoRef{gp_Pnt(60, 30, 10), {"F99/start"}}).has_value());
}

TEST(Naming, VertexFollowsAfterAFilletOnAnotherEdge)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const TopoRef corner = vertexAt(base, gp_Pnt(60, 30, 10));
    const TopoRef origin = vertexAt(base, gp_Pnt(0, 0, 0));
    b.doc.add(FilletFeature{{FilletSet{{edgeAt(base, gp_Pnt(30, 0, 10))}, 2.0}}});

    const Model filleted = recompute(b.doc);
    expectVertexAt(filleted, corner, gp_Pnt(60, 30, 10));
    expectVertexAt(filleted, origin, gp_Pnt(0, 0, 0));

    resize(b, Size{80.0, 15.0});
    const Model moved = recompute(b.doc);
    expectVertexAt(moved, corner, gp_Pnt(80, 30, 15));
    expectVertexAt(moved, origin, gp_Pnt(0, 0, 0));
}

TEST(Naming, VertexFollowsAfterACutSplitsAFace)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const TopoRef origin = vertexAt(base, gp_Pnt(0, 0, 0));
    const TopoRef far = vertexAt(base, gp_Pnt(60, 0, 0));
    const FeatureId groove = addRectangle(b.doc, 28, -5, 32, 35);
    const FeatureId cut = addExtrude(b.doc, groove, {30, 15}, 3.0, Operation::Cut);

    const Model grooved = recompute(b.doc);
    ASSERT_EQ(grooved.status.at(cut).health, FeatureHealth::Ok) << grooved.status.at(cut).message;
    expectVertexAt(grooved, origin, gp_Pnt(0, 0, 0));
    expectVertexAt(grooved, far, gp_Pnt(60, 0, 0));
    const TopoRef left = vertexAt(grooved, gp_Pnt(28, 0, 0));
    const TopoRef right = vertexAt(grooved, gp_Pnt(32, 0, 0));
    EXPECT_NE(left.names, right.names);
    expectVertexAt(grooved, left, gp_Pnt(28, 0, 0));
    expectVertexAt(grooved, right, gp_Pnt(32, 0, 0));

    resize(b, Size{80.0, 10.0});
    const Model wider = recompute(b.doc);
    expectVertexAt(wider, origin, gp_Pnt(0, 0, 0));
    expectVertexAt(wider, far, gp_Pnt(80, 0, 0));
    expectVertexAt(wider, left, gp_Pnt(28, 0, 0));
    expectVertexAt(wider, right, gp_Pnt(32, 0, 0));
}

TEST(Naming, RefsRoundTripAndOlderFilesLoad)
{
    Block b = block();
    const Model base = recompute(b.doc);
    const FeatureId fillet = b.doc.add(FilletFeature{{FilletSet{{edgeAt(base, gp_Pnt(30, 0, 10))}, 2.0}}});
    const FeatureId pull = b.doc.add(OffsetFaceFeature{{faceAt(base, gp_Pnt(30, 15, 10))}, 2.0});

    ShapeStore shapes;
    const std::string text = toJson(b.doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    const auto& edges = std::get<FilletFeature>(b.doc.find(fillet)->data).sets.front().edges;
    const auto& loadedEdges = std::get<FilletFeature>(loaded.find(fillet)->data).sets.front().edges;
    ASSERT_EQ(loadedEdges.size(), 1u);
    EXPECT_EQ(loadedEdges[0].names, edges[0].names);
    EXPECT_TRUE(loadedEdges[0].point.IsEqual(edges[0].point, 1e-12));

    nlohmann::json older = nlohmann::json::parse(text);
    older["version"] = 5;
    const std::function<void(nlohmann::json&)> toPoints = [&](nlohmann::json& j) {
        if (j.is_object() && j.contains("point") && j.contains("names")) {
            j = nlohmann::json(j.at("point"));
            return;
        }
        if (j.is_structured()) {
            for (nlohmann::json& child : j) {
                toPoints(child);
            }
        }
    };
    toPoints(older);
    Document legacy = fromJson(older.dump(), shapes);
    const auto& legacyFaces = std::get<OffsetFaceFeature>(legacy.find(pull)->data).faces;
    ASSERT_EQ(legacyFaces.size(), 1u);
    EXPECT_TRUE(legacyFaces[0].names.empty());
    EXPECT_TRUE(legacyFaces[0].point.IsEqual(gp_Pnt(30, 15, 10), 1e-12));
    const Model named = recompute(loaded);
    EXPECT_EQ(named.status.at(pull).health, FeatureHealth::Ok) << named.status.at(pull).message;
    const Model model = recompute(legacy);
    EXPECT_EQ(model.status.at(fillet).health, FeatureHealth::Ok);
    EXPECT_EQ(model.status.at(pull).health, FeatureHealth::Ok) << model.status.at(pull).message;
}

TEST(FaceLookup, FindsTheSameFaceAsCheckingEveryFace)
{
    const Model model = threeBodies();
    ASSERT_EQ(model.bodies.size(), 3u);
    std::vector<gp_Pnt> points{gp_Pnt(30, 5, 4), gp_Pnt(500, 500, 500)};
    for (const Body& body : model.bodies) {
        for (TopExp_Explorer it(body.shape, TopAbs_FACE); it.More(); it.Next()) {
            points.push_back(geometry::facePoint(TopoDS::Face(it.Current())));
        }
    }
    FaceLookup lookup;
    for (const gp_Pnt& point : points) {
        SCOPED_TRACE(std::to_string(point.X()) + " " + std::to_string(point.Y()) + " " + std::to_string(point.Z()));
        const std::optional<FaceHit> expected = everyFaceAt(model.bodies, point);
        expectSameHit(resolveFace(model.bodies, TopoRef{point, {}}), expected);
        expectSameHit(resolveFace(model.bodies, TopoRef{point, {}}, &lookup), expected);
        expectSameHit(resolveFace(model.bodies, TopoRef{point, {}}, &lookup), expected);
    }
}

TEST(FaceLookup, LooksAgainWhenABodyUpToTheHitChanges)
{
    const Model model = threeBodies();
    const gp_Pnt onSecond(50, 5, 8);
    FaceLookup lookup;
    const std::optional<FaceHit> first = lookup.atPoint(model.bodies, onSecond);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->body, 1u);

    std::vector<Body> movedAway = model.bodies;
    gp_Trsf away;
    away.SetTranslation(gp_Vec(0, 0, 100));
    movedAway[1].shape = movedAway[1].shape.Moved(TopLoc_Location(away));
    expectSameHit(lookup.atPoint(movedAway, onSecond), everyFaceAt(movedAway, onSecond));
    EXPECT_FALSE(lookup.atPoint(movedAway, onSecond).has_value());

    std::vector<Body> movedOnto = model.bodies;
    gp_Trsf onto;
    onto.SetTranslation(gp_Vec(40, 0, 0));
    movedOnto[0].shape = movedOnto[0].shape.Moved(TopLoc_Location(onto));
    const std::optional<FaceHit> hit = lookup.atPoint(movedOnto, onSecond);
    expectSameHit(hit, everyFaceAt(movedOnto, onSecond));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->body, 0u);

    std::vector<Body> laterMoved = model.bodies;
    laterMoved[2].shape = laterMoved[2].shape.Moved(TopLoc_Location(away));
    expectSameHit(lookup.atPoint(laterMoved, onSecond), first);

    std::vector<Body> earlierMoved = model.bodies;
    earlierMoved[0].shape = earlierMoved[0].shape.Moved(TopLoc_Location(away));
    expectSameHit(lookup.atPoint(earlierMoved, onSecond), first);
}

TEST(FaceLookup, FaceIndexIsNothingForAFaceTheShapeDoesntHold)
{
    const Model model = threeBodies();
    const TopoDS_Shape& first = model.bodies[0].shape;
    int index = 0;
    for (TopExp_Explorer it(first, TopAbs_FACE); it.More(); it.Next(), ++index) {
        EXPECT_EQ(faceIndexOf(first, TopoDS::Face(it.Current())), index);
    }
    const TopExp_Explorer other(model.bodies[1].shape, TopAbs_FACE);
    EXPECT_FALSE(faceIndexOf(first, TopoDS::Face(other.Current())).has_value());
}

TEST(FaceLookup, GivesTheCurrentFaceWhenAMoveRepeatsTheSamePlace)
{
    const Model model = threeBodies();
    const auto movedAgain = [&]() {
        std::vector<Body> bodies = model.bodies;
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(5, 0, 0));
        bodies[0].shape = bodies[0].shape.Moved(TopLoc_Location(shift));
        return bodies;
    };
    const gp_Pnt onFirst(15, 5, 8);
    const gp_Pnt onSecond(50, 5, 8);
    FaceLookup lookup;
    const std::vector<Body> once = movedAgain();
    expectSameHit(lookup.atPoint(once, onFirst), everyFaceAt(once, onFirst));
    expectSameHit(lookup.atPoint(once, onSecond), everyFaceAt(once, onSecond));

    const std::vector<Body> twice = movedAgain();
    ASSERT_FALSE(twice[0].shape.IsEqual(once[0].shape));
    expectSameHit(lookup.atPoint(twice, onFirst), everyFaceAt(twice, onFirst));
    expectSameHit(lookup.atPoint(twice, onSecond), everyFaceAt(twice, onSecond));
}
