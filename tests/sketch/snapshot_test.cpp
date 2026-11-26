// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/profiles.h"
#include "sketch/sketch.h"
#include "sketch/snapshot.h"
#include "snapshot/chord.h"
#include "snapshot/sketch.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Polygon3D.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

using namespace kalve137;
using namespace kalve137::sketch;

namespace {

constexpr double kTolerance = 1e-9;
constexpr double kWidth = 80.0;
constexpr double kHeight = 50.0;
constexpr double kHoleRadius = 10.0;
constexpr double kArcRadius = 30.0;

const gp_Ax3 kTilted(gp_Pnt(5.0, -3.0, 12.0), gp_Dir(1.0, 1.0, 1.0), gp_Dir(1.0, -1.0, 0.0));

struct Drawn {
    Sketch sketch;
    Id line = 0;
    Id hole = 0;
    Id arc = 0;
    Id guide = 0;
    Id fixed = 0;
};

Drawn drawn()
{
    Drawn d;
    Sketch& s = d.sketch;
    const std::array corners{s.addPoint({0.0, 0.0}), s.addPoint({kWidth, 0.0}), s.addPoint({kWidth, kHeight}),
                             s.addPoint({0.0, kHeight})};
    d.line = s.addLine(corners[0], corners[1]);
    for (std::size_t i = 1; i < corners.size(); ++i) {
        s.addLine(corners[i], corners[(i + 1) % corners.size()]);
    }
    d.hole = s.addCircle(s.addPoint({kWidth / 2.0, kHeight / 2.0}), kHoleRadius);
    const Id center = s.addPoint({200.0, 0.0});
    d.arc = s.addArc(center, s.addPoint({200.0 + kArcRadius, 0.0}), s.addPoint({200.0, kArcRadius}));
    d.guide = s.addLine(corners[0], corners[2], true);
    d.fixed = s.addPoint({-20.0, -20.0}, true);
    return d;
}

double distance3(const snapshot::Point& a, const gp_Pnt& b)
{
    return std::hypot(a[0] - b.X(), a[1] - b.Y(), a[2] - b.Z());
}

const snapshot::SketchCurve& curveOf(const snapshot::Sketch& shown, Id id)
{
    for (const snapshot::SketchCurve& curve : shown.curves) {
        if (curve.id == id) {
            return curve;
        }
    }
    throw std::out_of_range("no curve");
}

int occtNodes(const Sketch& sketch, Id curve, double deflection)
{
    const std::optional<TopoDS_Edge> edge = makeEdge(sketch, curve, kTilted);
    if (!edge) {
        return 0;
    }
    const BRepMesh_IncrementalMesh mesher(*edge, deflection, false, snapshot::kDeviationAngle, false);
    TopLoc_Location location;
    const Handle(Poly_Polygon3D) polygon = BRep_Tool::Polygon3D(*edge, location);
    return polygon.IsNull() ? 0 : polygon->NbNodes();
}

double area(const snapshot::SketchProfiles& profiles, std::uint32_t profile)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < profiles.triangles.size(); ++i) {
        if (profiles.profiles[i] != profile) {
            continue;
        }
        const auto& [a, b, c] = profiles.triangles[i];
        const snapshot::Point& p = profiles.vertices[a];
        const snapshot::Point& q = profiles.vertices[b];
        const snapshot::Point& r = profiles.vertices[c];
        const std::array u{q[0] - p[0], q[1] - p[1], q[2] - p[2]};
        const std::array v{r[0] - p[0], r[1] - p[1], r[2] - p[2]};
        sum += 0.5 *
               std::hypot((u[1] * v[2]) - (u[2] * v[1]), (u[2] * v[0]) - (u[0] * v[2]), (u[0] * v[1]) - (u[1] * v[0]));
    }
    return sum;
}

} // namespace

TEST(SketchSnapshot, CurvesBecomeWorldPolylinesWithKindIdAndConstructionFlag)
{
    const Drawn d = drawn();
    const snapshot::Sketch shown = snapshotOf(d.sketch, placementOf(kTilted), 42);
    EXPECT_EQ(shown.feature, 42);
    ASSERT_EQ(shown.curves.size(), d.sketch.curves().size());

    const snapshot::SketchCurve& line = curveOf(shown, d.line);
    EXPECT_EQ(line.kind, snapshot::SketchCurveKind::Line);
    EXPECT_FALSE(line.construction);
    ASSERT_EQ(line.points.size(), 2U);
    EXPECT_LT(distance3(line.points[0], toWorld(kTilted, {0.0, 0.0})), kTolerance);
    EXPECT_LT(distance3(line.points[1], toWorld(kTilted, {kWidth, 0.0})), kTolerance);

    const snapshot::SketchCurve& guide = curveOf(shown, d.guide);
    EXPECT_EQ(guide.kind, snapshot::SketchCurveKind::Line);
    EXPECT_TRUE(guide.construction);

    const snapshot::SketchCurve& hole = curveOf(shown, d.hole);
    EXPECT_EQ(hole.kind, snapshot::SketchCurveKind::Circle);
    EXPECT_EQ(hole.points.front(), hole.points.back()) << "circle closes on itself";
    const gp_Pnt holeCenter = toWorld(kTilted, {kWidth / 2.0, kHeight / 2.0});
    for (const snapshot::Point& point : hole.points) {
        EXPECT_NEAR(distance3(point, holeCenter), kHoleRadius, kTolerance);
    }

    const snapshot::SketchCurve& arc = curveOf(shown, d.arc);
    EXPECT_EQ(arc.kind, snapshot::SketchCurveKind::Arc);
    EXPECT_LT(distance3(arc.points.front(), toWorld(kTilted, {200.0 + kArcRadius, 0.0})), kTolerance);
    EXPECT_LT(distance3(arc.points.back(), toWorld(kTilted, {200.0, kArcRadius})), kTolerance);
    const gp_Pnt arcCenter = toWorld(kTilted, {200.0, 0.0});
    for (const snapshot::Point& point : arc.points) {
        EXPECT_NEAR(distance3(point, arcCenter), kArcRadius, kTolerance);
    }
}

TEST(SketchSnapshot, CurvesTessellateLikeBodyEdges)
{
    const Drawn d = drawn();
    const double side = 200.0 + kArcRadius;
    const double deflection = curveDeflection(d.sketch);
    EXPECT_NEAR(deflection, snapshot::chordDeflection(side), kTolerance) << "deflection follows the sketch's box";
    const snapshot::Sketch shown = snapshotOf(d.sketch, placementOf(kTilted));
    for (const Id id : {d.hole, d.arc}) {
        const snapshot::SketchCurve& curve = curveOf(shown, id);
        EXPECT_EQ(static_cast<int>(curve.points.size()), occtNodes(d.sketch, id, deflection)) << "curve " << id;
    }

    Sketch small;
    const Id lone = small.addCircle(small.addPoint({0.0, 0.0}), 2.0);
    const snapshot::Sketch alone = snapshotOf(small, placementOf(kTilted));
    ASSERT_EQ(alone.curves.size(), 1U);
    EXPECT_EQ(static_cast<int>(alone.curves[0].points.size()), occtNodes(small, lone, curveDeflection(small)));
    EXPECT_GE(alone.curves[0].points.size(), 37U) << "angle limit keeps a lone circle round";

    Sketch flat;
    const double angle = 6.0 * std::numbers::pi / 180.0;
    const Id center = flat.addPoint({0.0, 0.0});
    const Id wide = flat.addArc(center, flat.addPoint({1000.0, 0.0}),
                                flat.addPoint({1000.0 * std::cos(angle), 1000.0 * std::sin(angle)}));
    const snapshot::Sketch shallow = snapshotOf(flat, placementOf(kTilted));
    ASSERT_EQ(shallow.curves.size(), 1U);
    EXPECT_LT(shallow.curves[0].points.size(), 6U) << "deflection limit rules a shallow arc";
    EXPECT_EQ(static_cast<int>(shallow.curves[0].points.size()), occtNodes(flat, wide, curveDeflection(flat)));

    Sketch crowded;
    crowded.addLine(crowded.addPoint({0.0, 0.0}), crowded.addPoint({1000.0, 0.0}));
    const Id tiny = crowded.addCircle(crowded.addPoint({500.0, 20.0}), 1.0);
    const snapshot::Sketch dense = snapshotOf(crowded, placementOf(kTilted));
    const snapshot::SketchCurve& dot = curveOf(dense, tiny);
    EXPECT_LT(dot.points.size(), 37U) << "minimum size rules a tiny circle in a large sketch";
    EXPECT_EQ(static_cast<int>(dot.points.size()), occtNodes(crowded, tiny, curveDeflection(crowded)));
}

TEST(SketchSnapshot, PointsCarryTheirIdsAndFlags)
{
    const Drawn d = drawn();
    const snapshot::Sketch shown = snapshotOf(d.sketch, placementOf(kTilted));
    ASSERT_EQ(shown.points.size(), d.sketch.points().size());
    std::map<int, std::uint8_t> flags;
    for (const snapshot::SketchPoint& point : shown.points) {
        flags[point.id] = point.flags;
        EXPECT_LT(distance3(point.point, toWorld(kTilted, d.sketch.point(point.id)->pos)), kTolerance);
    }
    EXPECT_EQ(flags.at(kOriginId), snapshot::kSketchPointFixed | snapshot::kSketchPointOrigin);
    EXPECT_EQ(flags.at(d.fixed), snapshot::kSketchPointFixed);
    EXPECT_EQ(flags.at(d.sketch.curve(d.line)->points[0]), 0);
}

TEST(SketchSnapshot, ProfilesBecomeTrianglesWithTheirIndex)
{
    const Drawn d = drawn();
    const std::vector<TopoDS_Face> faces = findProfiles(d.sketch, kTilted);
    ASSERT_EQ(faces.size(), 2U);
    const snapshot::Sketch shown = snapshotOf(d.sketch, kTilted, faces, 3);
    const snapshot::SketchProfiles& profiles = shown.profiles;
    EXPECT_EQ(profiles.count, 2U);
    ASSERT_EQ(profiles.profiles.size(), profiles.triangles.size());
    EXPECT_EQ(std::set(profiles.profiles.begin(), profiles.profiles.end()), (std::set<std::uint32_t>{0, 1}));
    const double disk = std::numbers::pi * kHoleRadius * kHoleRadius;
    const double deflection = curveDeflection(d.sketch);
    const double rim = 2.0 * std::numbers::pi * kHoleRadius * deflection;
    const double first = area(profiles, 0);
    const double second = area(profiles, 1);
    EXPECT_NEAR(std::min(first, second), disk, rim) << "hole fills as its own profile";
    EXPECT_NEAR(std::max(first, second), (kWidth * kHeight) - disk, rim) << "plate keeps the hole open";
    EXPECT_NEAR(first + second, kWidth * kHeight, rim);
    const std::uint32_t hole = first < second ? 0 : 1;
    EXPECT_EQ(profiles.disks, std::vector<std::uint32_t>{hole}) << "only the hole is bounded by one full circle";
    for (const snapshot::Point& vertex : profiles.vertices) {
        const Vec2 local = toSketch(kTilted, gp_Pnt(vertex[0], vertex[1], vertex[2]));
        EXPECT_LT(distance3(vertex, toWorld(kTilted, local)), 1e-6) << "every vertex lies in the sketch plane";
    }
}
