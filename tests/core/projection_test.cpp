// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/document.h"
#include "core/naming.h"
#include "core/projection.h"
#include "core/recompute.h"
#include "core/serialization.h"
#include "geometry/modeling.h"
#include "sketch/edit.h"
#include "sketch/profiles.h"
#include "sketch/solver.h"

#include <gtest/gtest.h>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>

#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace kalve137;
using namespace kalve137::core;

namespace {

constexpr double kTolerance = 1e-9;

const gp_Ax3 kFloor(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
const gp_Ax3 kTurned(gp_Pnt(1, 2, 3), gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
const gp_Ax3 kUnder(gp_Pnt(0, 0, -4), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0));

TopoDS_Edge line(const gp_Pnt& from, const gp_Pnt& to)
{
    return BRepBuilderAPI_MakeEdge(from, to);
}

void expectNear(sketch::Vec2 actual, sketch::Vec2 expected)
{
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
}

gp_Pnt midpoint(const TopoDS_Edge& edge)
{
    const BRepAdaptor_Curve curve(edge);
    return curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);
}

gp_Pnt ontoPlane(const gp_Pnt& point, const gp_Ax3& plane)
{
    const gp_Vec normal(plane.Direction());
    return point.Translated(normal * -gp_Vec(plane.Location(), point).Dot(normal));
}

double distanceTo(const TopoDS_Shape& shape, const gp_Pnt& point)
{
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(), shape);
    return distance.IsDone() ? distance.Value() : 1e9;
}

constexpr double kModelTolerance = 1e-6;

struct Part {
    Document doc;
    FeatureId base = 0;
    FeatureId pad = 0;
    FeatureId plane = 0;
    sketch::Id circle = 0;
    std::vector<sketch::Id> right;
};

Part cylinder(double radius, double height)
{
    Part part;
    SketchFeature base;
    part.circle = base.sketch.addCircle(sketch::kOriginId, radius);
    part.base = part.doc.add(std::move(base));
    ExtrudeFeature pad;
    pad.profiles = {part.base, {{0, 0}}};
    pad.distance = height;
    pad.bodyName = part.doc.takeBodyName();
    part.pad = part.doc.add(std::move(pad));
    return part;
}

Part block(double width, double height)
{
    Part part;
    SketchFeature base;
    sketch::Sketch& s = base.sketch;
    const sketch::Id b = s.addPoint({width, 0});
    const sketch::Id c = s.addPoint({width, 20});
    const sketch::Id d = s.addPoint({0, 20});
    s.addLine(sketch::kOriginId, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, sketch::kOriginId);
    part.right = {b, c};
    part.base = part.doc.add(std::move(base));
    ExtrudeFeature pad;
    pad.profiles = {part.base, {{1, 1}}};
    pad.distance = height;
    pad.bodyName = part.doc.takeBodyName();
    part.pad = part.doc.add(std::move(pad));
    return part;
}

void setWidth(Part& part, double width)
{
    sketch::Sketch& s = std::get<SketchFeature>(part.doc.find(part.base)->data).sketch;
    for (const sketch::Id id : part.right) {
        s.point(id)->pos.x = width;
    }
}

void setHeight(Part& part, double height)
{
    std::get<ExtrudeFeature>(part.doc.find(part.pad)->data).distance = height;
}

SketchFeature sketchAbove(Part& part, double offset)
{
    PlaneFeature plane;
    plane.baseName = "XY";
    plane.offset = offset;
    part.plane = part.doc.add(plane);
    SketchFeature data;
    data.planeFeature = part.plane;
    data.planeName = "Plane1";
    data.plane = recompute(part.doc).planes.at(part.plane).frame;
    return data;
}

Projection projectEdgeAt(SketchFeature& data, const Model& model, const gp_Pnt& at)
{
    const Body& body = model.bodies.at(0);
    const std::optional<TopoDS_Edge> edge = geometry::findEdge(body.shape, at, kModelTolerance);
    if (!edge) {
        ADD_FAILURE() << "No edge at " << at.X() << " " << at.Y() << " " << at.Z();
        return {};
    }
    const std::optional<ProjectedEdge> projected = projectEdge(*edge, data.plane);
    if (!projected) {
        ADD_FAILURE() << "Edge refused at " << at.X() << " " << at.Y() << " " << at.Z();
        return {};
    }
    return addProjectedEdge(data, *projected, edgeRef(body, *edge, at));
}

Projection projectVertexAt(SketchFeature& data, const Model& model, const gp_Pnt& at)
{
    const Body& body = model.bodies.at(0);
    for (TopExp_Explorer it(body.shape, TopAbs_VERTEX); it.More(); it.Next()) {
        const TopoDS_Vertex& vertex = TopoDS::Vertex(it.Current());
        if (BRep_Tool::Pnt(vertex).Distance(at) < kModelTolerance) {
            return addProjectedVertex(data, *projectVertex(vertex, data.plane), vertexRef(body, vertex));
        }
    }
    ADD_FAILURE() << "No vertex at " << at.X() << " " << at.Y() << " " << at.Z();
    return {};
}

const sketch::Sketch& solvedOf(const Model& model, FeatureId id)
{
    return model.sketches.at(id).solved;
}

sketch::Vec2 pointOf(const Model& model, FeatureId id, sketch::Id point)
{
    return solvedOf(model, id).point(point)->pos;
}

FeatureHealth healthOf(const Model& model, FeatureId id)
{
    const auto it = model.status.find(id);
    return it == model.status.end() ? FeatureHealth::Ok : it->second.health;
}

} // namespace

TEST(Projection, StraightEdgeDropsAlongTheSketchNormal)
{
    const TopoDS_Edge edge = line(gp_Pnt(0, 0, 5), gp_Pnt(10, 5, 8));
    const std::optional<ProjectedEdge> flat = projectEdge(edge, kFloor);
    ASSERT_TRUE(flat.has_value());
    EXPECT_EQ(flat->shape, ProjectedShape::Line);
    ASSERT_EQ(flat->points.size(), 2U);
    expectNear(flat->points[0], {0, 0});
    expectNear(flat->points[1], {10, 5});

    const std::optional<ProjectedEdge> turned = projectEdge(edge, kTurned);
    ASSERT_TRUE(turned.has_value());
    ASSERT_EQ(turned->points.size(), 2U);
    expectNear(turned->points[0], {-2, 1});
    expectNear(turned->points[1], {3, -9});
}

TEST(Projection, EdgeAlongTheNormalBecomesAPoint)
{
    const std::optional<ProjectedEdge> point = projectEdge(line(gp_Pnt(4, 6, 0), gp_Pnt(4, 6, 9)), kFloor);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->shape, ProjectedShape::Point);
    ASSERT_EQ(point->points.size(), 1U);
    expectNear(point->points[0], {4, 6});
}

TEST(Projection, CircleParallelToTheSketchKeepsItsRadius)
{
    const gp_Circ circle(gp_Ax2(gp_Pnt(5, 5, 7), gp_Dir(0, 0, 1)), 3.0);
    const TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circle);
    for (const auto& [plane, center] :
         {std::pair{kFloor, sketch::Vec2{5, 5}}, std::pair{kUnder, sketch::Vec2{5, -5}}}) {
        const std::optional<ProjectedEdge> projected = projectEdge(edge, plane);
        ASSERT_TRUE(projected.has_value());
        EXPECT_EQ(projected->shape, ProjectedShape::Circle);
        EXPECT_NEAR(projected->radius, 3.0, kTolerance);
        ASSERT_EQ(projected->points.size(), 1U);
        expectNear(projected->points[0], center);
    }
}

TEST(Projection, ArcKeepsItsSweepSeenFromEitherSide)
{
    const gp_Circ circle(gp_Ax2(gp_Pnt(5, 5, 7), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 3.0);
    const TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circle, 0.0, std::numbers::pi / 2.0);
    const std::optional<ProjectedEdge> floor = projectEdge(edge, kFloor);
    ASSERT_TRUE(floor.has_value());
    EXPECT_EQ(floor->shape, ProjectedShape::Arc);
    ASSERT_EQ(floor->points.size(), 3U);
    expectNear(floor->points[0], {5, 5});
    expectNear(floor->points[1], {8, 5});
    expectNear(floor->points[2], {5, 8});

    const std::optional<ProjectedEdge> under = projectEdge(edge, kUnder);
    ASSERT_TRUE(under.has_value());
    ASSERT_EQ(under->points.size(), 3U);
    expectNear(under->points[0], {5, -5});
    expectNear(under->points[1], {5, -8});
    expectNear(under->points[2], {8, -5});

    for (const gp_Ax3& plane : {kFloor, kUnder}) {
        SketchFeature data;
        data.plane = plane;
        const Projection link = addProjectedEdge(data, *projectEdge(edge, plane), midpoint(edge));
        const std::optional<TopoDS_Edge> drawn = sketch::makeEdge(data.sketch, link.curve, plane);
        ASSERT_TRUE(drawn.has_value());
        EXPECT_LT(distanceTo(*drawn, ontoPlane(midpoint(edge), plane)), 1e-7);
        EXPECT_NEAR(data.sketch.curve(link.curve)->radius, 3.0, kTolerance);
    }
}

TEST(Projection, OtherCurvesAreRefused)
{
    const gp_Circ tilted(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 1)), 2.0);
    EXPECT_FALSE(projectEdge(BRepBuilderAPI_MakeEdge(tilted), kFloor).has_value());
    const gp_Elips ellipse(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 4.0, 2.0);
    EXPECT_FALSE(projectEdge(BRepBuilderAPI_MakeEdge(ellipse), kFloor).has_value());
}

TEST(Projection, TrimFilletAndDeleteAroundProjectedItemsKeepTheLinks)
{
    Part part = block(40.0, 10.0);
    SketchFeature data = sketchAbove(part, 25.0);
    const Model model = recompute(part.doc);
    const Projection edge = projectEdgeAt(data, model, gp_Pnt(20, 0, 10));
    const Projection vertex = projectVertexAt(data, model, gp_Pnt(40, 20, 10));
    ASSERT_NE(edge.curve, 0);
    ASSERT_NE(vertex.point, 0);
    sketch::Sketch& s = data.sketch;
    const std::vector<sketch::Id> ends = s.curve(edge.curve)->points;
    const sketch::Vec2 corner = s.point(ends[0])->pos;
    s.addLine(s.addPoint({20, -10}), s.addPoint({20, 10}));
    const sketch::Id leg = s.addLine(ends[0], s.addPoint(corner + sketch::Vec2{0, -15}));
    s.addLine(s.addPoint({20, 20}), vertex.point);
    s.addLine(vertex.point, s.addPoint({40, 40}));

    EXPECT_FALSE(sketch::trim(s, edge.curve, {5, 0}));
    EXPECT_FALSE(sketch::filletCorner(s, ends[0], 2.0));
    const std::optional<sketch::Sketch> rounded = sketch::filletCorner(s, vertex.point, 3.0);
    ASSERT_TRUE(rounded);
    s = *rounded;
    ASSERT_NE(s.point(vertex.point), nullptr);
    EXPECT_TRUE(s.point(vertex.point)->projected);
    EXPECT_TRUE(s.curvesUsing(vertex.point).empty());
    EXPECT_FALSE(validateProjections(data));

    s.remove(edge.curve);
    dropBrokenProjections(data);
    ASSERT_EQ(data.projections.size(), 1U);
    EXPECT_EQ(data.projections[0].point, vertex.point);
    EXPECT_EQ(s.point(ends[1]), nullptr);
    ASSERT_NE(s.point(ends[0]), nullptr);
    EXPECT_FALSE(s.point(ends[0])->projected);
    EXPECT_FALSE(s.point(ends[0])->fixed);
    EXPECT_NE(s.curve(leg), nullptr);
    EXPECT_FALSE(validateProjections(data));
    EXPECT_FALSE(sketch::validate(s));

    const FeatureId id = part.doc.add(std::move(data));
    const Model after = recompute(part.doc);
    EXPECT_EQ(healthOf(after, id), FeatureHealth::Ok);
    expectNear(pointOf(after, id, vertex.point), {40, 20});

    ShapeStore shapes;
    const std::string text = toJson(part.doc, shapes);
    const Document loaded = fromJson(text, shapes);
    EXPECT_EQ(toJson(loaded, shapes), text);
    EXPECT_EQ(healthOf(recompute(loaded), id), FeatureHealth::Ok);
}

TEST(Projection, ADroppedLinkReleasesItsItems)
{
    SketchFeature data;
    const Projection line = addProjectedEdge(data, {ProjectedShape::Line, {{0, 0}, {10, 0}}, 0.0}, gp_Pnt(5, 0, 0));
    sketch::Sketch& s = data.sketch;
    const std::vector<sketch::Id> ends = s.curve(line.curve)->points;
    s.point(ends[0])->projected = false;
    EXPECT_TRUE(validateProjections(data));
    dropBrokenProjections(data);
    EXPECT_TRUE(data.projections.empty());
    EXPECT_FALSE(s.curve(line.curve)->projected);
    EXPECT_FALSE(s.curve(line.curve)->fixed);
    EXPECT_FALSE(s.point(ends[1])->projected);
    EXPECT_FALSE(s.point(ends[1])->fixed);
    EXPECT_FALSE(validateProjections(data));

    const Projection circle = addProjectedEdge(data, {ProjectedShape::Circle, {{20, 0}}, 3.0}, gp_Pnt(23, 0, 0));
    EXPECT_FALSE(validateProjections(data));
    s.point(s.curve(circle.curve)->points[0])->projected = false;
    EXPECT_TRUE(validateProjections(data));
    s.point(s.curve(circle.curve)->points[0])->projected = true;
    s.point(s.addPoint({5, 5}, true))->projected = true;
    EXPECT_TRUE(validateProjections(data));
}

TEST(Projection, VertexDropsOntoThePlane)
{
    const std::optional<sketch::Vec2> at = projectVertex(BRepBuilderAPI_MakeVertex(gp_Pnt(3, 4, 9)), kTurned);
    ASSERT_TRUE(at.has_value());
    expectNear(*at, {2, -2});
}

TEST(Projection, ProjectedItemsAreFixedAndLinked)
{
    SketchFeature data;
    const Projection line = addProjectedEdge(data, {ProjectedShape::Line, {{0, 0}, {10, 0}}, 0.0}, gp_Pnt(5, 0, 0));
    const Projection end = addProjectedEdge(data, {ProjectedShape::Point, {{4, 6}}, 0.0}, gp_Pnt(4, 6, 3));
    const Projection vertex = addProjectedVertex(data, {1, 2}, gp_Pnt(1, 2, 0));
    const sketch::Sketch& s = data.sketch;

    EXPECT_EQ(line.kind, ProjectionKind::Edge);
    EXPECT_EQ(line.point, 0);
    const sketch::Curve* curve = s.curve(line.curve);
    ASSERT_NE(curve, nullptr);
    EXPECT_TRUE(curve->projected);
    EXPECT_TRUE(curve->fixed);
    for (const sketch::Id id : curve->points) {
        EXPECT_TRUE(s.point(id)->projected);
        EXPECT_TRUE(s.point(id)->fixed);
    }

    EXPECT_EQ(end.kind, ProjectionKind::Edge);
    EXPECT_EQ(end.curve, 0);
    ASSERT_NE(s.point(end.point), nullptr);
    EXPECT_TRUE(s.point(end.point)->projected);

    EXPECT_EQ(vertex.kind, ProjectionKind::Vertex);
    ASSERT_NE(s.point(vertex.point), nullptr);
    EXPECT_TRUE(s.point(vertex.point)->projected);

    EXPECT_EQ(data.projections.size(), 3U);
    EXPECT_FALSE(validateProjections(data).has_value());
    EXPECT_FALSE(sketch::validate(s).has_value());
}

TEST(Projection, DroppingBrokenLinksKeepsTheRest)
{
    SketchFeature data;
    const Projection line = addProjectedEdge(data, {ProjectedShape::Line, {{0, 0}, {10, 0}}, 0.0}, gp_Pnt(5, 0, 0));
    const Projection circle = addProjectedEdge(data, {ProjectedShape::Circle, {{20, 0}}, 3.0}, gp_Pnt(23, 0, 0));
    const std::vector<sketch::Id> ends = data.sketch.curve(line.curve)->points;
    data.sketch.remove(line.curve);
    ASSERT_TRUE(validateProjections(data).has_value());
    dropBrokenProjections(data);
    for (const sketch::Id end : ends) {
        EXPECT_EQ(data.sketch.point(end), nullptr);
    }
    ASSERT_EQ(data.projections.size(), 1U);
    EXPECT_EQ(data.projections[0].curve, circle.curve);
    EXPECT_FALSE(validateProjections(data).has_value());
}

TEST(Projection, CylinderTopEdgeFollowsDiameterAndHeight)
{
    Part part = cylinder(10.0, 20.0);
    SketchFeature data = sketchAbove(part, 30.0);
    const Projection link = projectEdgeAt(data, recompute(part.doc), gp_Pnt(0, 10, 20));
    ASSERT_NE(link.curve, 0);
    const FeatureId id = part.doc.add(std::move(data));

    for (const auto& [radius, height] : {std::pair{10.0, 20.0}, std::pair{15.0, 20.0}, std::pair{15.0, 40.0}}) {
        std::get<SketchFeature>(part.doc.find(part.base)->data).sketch.curve(part.circle)->radius = radius;
        setHeight(part, height);
        const Model model = recompute(part.doc);
        EXPECT_EQ(healthOf(model, id), FeatureHealth::Ok);
        const sketch::Curve* curve = solvedOf(model, id).curve(link.curve);
        ASSERT_NE(curve, nullptr);
        EXPECT_NEAR(curve->radius, radius, kTolerance);
        expectNear(pointOf(model, id, curve->points[0]), {0, 0});
        ASSERT_EQ(model.sketches.at(id).profiles.size(), 1U);
    }
}

TEST(Projection, LineEdgeKeepsItsEndsWhenTheModelGrows)
{
    Part part = block(40.0, 10.0);
    SketchFeature data = sketchAbove(part, 25.0);
    const Projection link = projectEdgeAt(data, recompute(part.doc), gp_Pnt(20, 0, 10));
    ASSERT_NE(link.curve, 0);
    const std::vector<sketch::Id> ends = data.sketch.curve(link.curve)->points;
    const sketch::Vec2 start = data.sketch.point(ends[0])->pos;
    const FeatureId id = part.doc.add(std::move(data));

    setWidth(part, 60.0);
    setHeight(part, 15.0);
    const Model model = recompute(part.doc);
    EXPECT_EQ(healthOf(model, id), FeatureHealth::Ok);
    const sketch::Vec2 a = pointOf(model, id, ends[0]);
    const sketch::Vec2 b = pointOf(model, id, ends[1]);
    EXPECT_LT(sketch::distance(a, start), kModelTolerance);
    expectNear(start.x < 1.0 ? b : a, {60, 0});
}

TEST(Projection, ArcFollowsAFilletRadius)
{
    Part part = block(40.0, 10.0);
    const Model base = recompute(part.doc);
    const Body& body = base.bodies.at(0);
    const gp_Pnt corner(40, 20, 5);
    FilletFeature fillet{
        {FilletSet{{edgeRef(body, *geometry::findEdge(body.shape, corner, kModelTolerance), corner)}, 4.0}}};
    const FeatureId filletId = part.doc.add(fillet);
    SketchFeature data = sketchAbove(part, 20.0);
    const double side = 4.0 - 4.0 / std::numbers::sqrt2;
    const Projection link = projectEdgeAt(data, recompute(part.doc), gp_Pnt(40 - side, 20 - side, 10));
    ASSERT_NE(link.curve, 0);
    EXPECT_EQ(data.sketch.curve(link.curve)->kind, sketch::CurveKind::Arc);
    const FeatureId id = part.doc.add(std::move(data));

    std::get<FilletFeature>(part.doc.find(filletId)->data).sets[0].radius = 6.0;
    const Model model = recompute(part.doc);
    EXPECT_EQ(healthOf(model, id), FeatureHealth::Ok);
    const sketch::Curve* arc = solvedOf(model, id).curve(link.curve);
    EXPECT_NEAR(arc->radius, 6.0, kTolerance);
    expectNear(pointOf(model, id, arc->points[0]), {34, 14});
}

TEST(Projection, VertexAndUprightEdgeFollowTheModel)
{
    Part part = block(40.0, 10.0);
    SketchFeature data = sketchAbove(part, 25.0);
    const Model base = recompute(part.doc);
    const Projection vertex = projectVertexAt(data, base, gp_Pnt(40, 20, 10));
    const Projection upright = projectEdgeAt(data, base, gp_Pnt(40, 0, 5));
    ASSERT_NE(vertex.point, 0);
    ASSERT_NE(upright.point, 0);
    const FeatureId id = part.doc.add(std::move(data));

    setWidth(part, 55.0);
    const Model model = recompute(part.doc);
    EXPECT_EQ(healthOf(model, id), FeatureHealth::Ok);
    expectNear(pointOf(model, id, vertex.point), {55, 20});
    expectNear(pointOf(model, id, upright.point), {55, 0});
}

TEST(Projection, DimensionFromAProjectedPointHolds)
{
    Part part = block(40.0, 10.0);
    SketchFeature data = sketchAbove(part, 25.0);
    const Projection vertex = projectVertexAt(data, recompute(part.doc), gp_Pnt(40, 20, 10));
    ASSERT_NE(vertex.point, 0);
    const sketch::Id loose = data.sketch.addPoint({40, 30});
    data.sketch.addConstraint(sketch::ConstraintKind::Distance, {vertex.point, loose}, 10.0);
    const FeatureId id = part.doc.add(std::move(data));

    setWidth(part, 60.0);
    const Model model = recompute(part.doc);
    EXPECT_EQ(healthOf(model, id), FeatureHealth::Ok);
    expectNear(pointOf(model, id, vertex.point), {60, 20});
    EXPECT_NEAR(sketch::distance(pointOf(model, id, vertex.point), pointOf(model, id, loose)), 10.0, 1e-6);
}

TEST(Projection, LostSourceKeepsTheLastGeometryAndWarns)
{
    Part part = cylinder(10.0, 20.0);
    SketchFeature data = sketchAbove(part, 30.0);
    const Model base = recompute(part.doc);
    const Projection edge = projectEdgeAt(data, base, gp_Pnt(0, 10, 20));
    const Projection vertex = projectEdgeAt(data, base, gp_Pnt(10, 0, 10));
    ASSERT_NE(edge.curve, 0);
    ASSERT_NE(vertex.point, 0);
    const FeatureId id = part.doc.add(std::move(data));

    std::get<SketchFeature>(part.doc.find(part.base)->data).sketch.curve(part.circle)->radius = 15.0;
    part.doc.remove(part.pad);
    const Model model = recompute(part.doc);
    ASSERT_EQ(healthOf(model, id), FeatureHealth::Warning);
    EXPECT_EQ(model.status.at(id).message, brokenProjectionsMessage(2));
    EXPECT_EQ(brokenProjectionsMessage(2), "2 projected curves lost their model edge");
    const sketch::Curve* curve = solvedOf(model, id).curve(edge.curve);
    ASSERT_NE(curve, nullptr);
    EXPECT_NEAR(curve->radius, 10.0, kTolerance);
    EXPECT_TRUE(model.sketches.at(id).solve.solved);
    EXPECT_TRUE(model.sketches.at(id).solve.conflicting.empty());
    expectNear(pointOf(model, id, vertex.point), {10, 0});
}

TEST(Projection, WarningListsEveryProblem)
{
    Part part = cylinder(10.0, 20.0);
    SketchFeature data = sketchAbove(part, 30.0);
    const Projection edge = projectEdgeAt(data, recompute(part.doc), gp_Pnt(0, 10, 20));
    ASSERT_NE(edge.curve, 0);
    const sketch::Id from = data.sketch.addPoint({30, 0});
    const sketch::Id to = data.sketch.addPoint({40, 0});
    data.sketch.addConstraint(sketch::ConstraintKind::Distance, {from, to}, 10.0);
    data.sketch.addConstraint(sketch::ConstraintKind::Distance, {from, to}, 20.0);
    const FeatureId id = part.doc.add(std::move(data));

    part.doc.remove(part.pad);
    const Model model = recompute(part.doc);
    ASSERT_EQ(healthOf(model, id), FeatureHealth::Warning);
    EXPECT_FALSE(model.sketches.at(id).solve.solved);
    EXPECT_EQ(model.status.at(id).message, brokenProjectionsMessage(1) + ". The sketch has conflicting constraints");
}

TEST(Projection, HorizontalDistanceKeepsItsSideWhenTheModelMovesPastIt)
{
    Part part = block(40.0, 10.0);
    SketchFeature data = sketchAbove(part, 25.0);
    const Projection vertex = projectVertexAt(data, recompute(part.doc), gp_Pnt(40, 20, 10));
    ASSERT_NE(vertex.point, 0);
    const sketch::Id loose = data.sketch.addPoint({45, 30});
    data.sketch.addConstraint(sketch::ConstraintKind::HorizontalDistance, {vertex.point, loose}, 5.0);
    const FeatureId id = part.doc.add(std::move(data));

    setWidth(part, 60.0);
    const Model model = recompute(part.doc);
    EXPECT_EQ(healthOf(model, id), FeatureHealth::Ok);
    expectNear(pointOf(model, id, vertex.point), {60, 20});
    EXPECT_NEAR(pointOf(model, id, loose).x, 65.0, 1e-6);
}

TEST(Projection, VerticalDistanceAndAngleKeepTheirSideWhenProjectionsMove)
{
    sketch::Sketch before;
    const sketch::Id a = before.addPoint({0, 0}, true);
    const sketch::Id b = before.addPoint({0, 10}, true);
    const sketch::Id fixedLine = before.addLine(a, b);
    const sketch::Id e = before.addPoint({10, 10});
    const sketch::Id freeLine = before.addLine(a, e);
    const sketch::Id loose = before.addPoint({20, 5});
    before.addConstraint(sketch::ConstraintKind::VerticalDistance, {a, loose}, 5.0);
    before.addConstraint(sketch::ConstraintKind::Angle, {fixedLine, freeLine}, 45.0);

    sketch::Sketch moved = before;
    moved.point(a)->pos = {20, 10};
    moved.point(b)->pos = {20, 20};
    ASSERT_TRUE(sketch::solveMoved(moved, before).solved);
    EXPECT_NEAR(moved.point(loose)->pos.y, 15.0, 1e-6);
    const sketch::Vec2 up = moved.point(b)->pos - moved.point(a)->pos;
    const sketch::Vec2 out = moved.point(e)->pos - moved.point(a)->pos;
    EXPECT_LT(up.x * out.y - up.y * out.x, 0.0);
}

TEST(Projection, AnOpenCascadeFailureBreaksOnlyItsLink)
{
    SketchFeature data;
    const Projection kept = addProjectedEdge(data, {ProjectedShape::Line, {{0, 0}, {10, 0}}, 0.0}, gp_Pnt(5, 0, 0));
    const Projection failing =
        addProjectedEdge(data, {ProjectedShape::Line, {{0, 20}, {10, 20}}, 0.0}, gp_Pnt(0, 20, 0));
    BRep_Builder builder;
    TopoDS_Edge bare;
    builder.MakeEdge(bare);
    builder.Add(bare, BRepBuilderAPI_MakeVertex(gp_Pnt(0, 20, 0)).Vertex().Oriented(TopAbs_FORWARD));
    builder.Add(bare, BRepBuilderAPI_MakeVertex(gp_Pnt(10, 20, 0)).Vertex().Oriented(TopAbs_REVERSED));
    std::vector<Body> bodies(2);
    bodies[0].shape = line(gp_Pnt(0, 0, 0), gp_Pnt(12, 0, 0));
    bodies[1].shape = bare;
    EXPECT_ANY_THROW({
        if (const std::optional<EdgeHit> hit = resolveEdge(bodies, failing.ref)) {
            projectEdge(hit->edge, kFloor);
        }
    });

    const std::vector<sketch::Id> broken = followProjections(data.sketch, data.projections, bodies, kFloor);
    EXPECT_EQ(broken, std::vector<sketch::Id>{failing.curve});
    const std::vector<sketch::Id> ends = data.sketch.curve(kept.curve)->points;
    const sketch::Vec2 far =
        data.sketch.point(ends[0])->pos.x > 1.0 ? data.sketch.point(ends[0])->pos : data.sketch.point(ends[1])->pos;
    expectNear(far, {12, 0});
}
