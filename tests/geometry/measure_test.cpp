// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/measure.h"
#include "geometry/shapes.h"

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <gtest/gtest.h>

#include <numbers>
#include <vector>

namespace {

using namespace kalve137::geometry;

std::vector<TopoDS_Shape> partsOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum kind)
{
    std::vector<TopoDS_Shape> parts;
    for (TopExp_Explorer it(shape, kind); it.More(); it.Next()) {
        parts.push_back(it.Current());
    }
    return parts;
}

TEST(Measure, BoxFacesDistanceAndAngle)
{
    const TopoDS_Shape box = makeBox(10.0, 20.0, 30.0);
    const std::vector<TopoDS_Shape> faces = partsOf(box, TopAbs_FACE);
    ASSERT_EQ(faces.size(), 6u);
    const Measurement opposite = measure(faces[0], faces[1]);
    ASSERT_TRUE(opposite.distance);
    EXPECT_NEAR(*opposite.distance, 10.0, 1e-9);
    ASSERT_TRUE(opposite.angle);
    EXPECT_NEAR(*opposite.angle, 0.0, 1e-9);

    const Measurement adjacent = measure(faces[0], faces[2]);
    EXPECT_NEAR(*adjacent.distance, 0.0, 1e-9);
    EXPECT_NEAR(*adjacent.angle, 90.0, 1e-9);

    EXPECT_NEAR(*measure(faces[4]).area, 200.0, 1e-6);
}

TEST(Measure, EdgeLengthAndCircleRadius)
{
    const TopoDS_Shape cylinder = makeCylinder(gp_Pnt(0, 0, 0), 5.0, 12.0);
    bool sawCircle = false;
    for (const TopoDS_Shape& edge : partsOf(cylinder, TopAbs_EDGE)) {
        const Measurement m = measure(edge);
        ASSERT_TRUE(m.length);
        if (m.radius) {
            sawCircle = true;
            EXPECT_NEAR(*m.radius, 5.0, 1e-9);
            EXPECT_NEAR(*m.length, 2.0 * std::numbers::pi * 5.0, 1e-6);
        }
    }
    EXPECT_TRUE(sawCircle);
}

TEST(Measure, LineAgainstFaceAngle)
{
    const TopoDS_Shape box = makeBox(10.0, 10.0, 10.0);
    const std::vector<TopoDS_Shape> faces = partsOf(box, TopAbs_FACE);
    for (const TopoDS_Shape& edge : partsOf(box, TopAbs_EDGE)) {
        const Measurement m = measure(edge, faces[4]);
        ASSERT_TRUE(m.angle);
        EXPECT_TRUE(std::abs(*m.angle) < 1e-9 || std::abs(*m.angle - 90.0) < 1e-9);
    }
}

TEST(Properties, BoxWithHole)
{
    const TopoDS_Shape block = cut(makeBox(20.0, 20.0, 10.0), makeCylinder(gp_Pnt(5.0, 10.0, 0.0), 2.0, 10.0));
    const BodyProperties p = properties(block);
    const double hole = std::numbers::pi * 4.0 * 10.0;
    EXPECT_NEAR(p.volume, 4000.0 - hole, 1e-6);
    EXPECT_NEAR(p.area, 2.0 * 400.0 + 4.0 * 200.0 - 2.0 * std::numbers::pi * 4.0 + 2.0 * std::numbers::pi * 2.0 * 10.0,
                1e-6);
    EXPECT_NEAR(p.centerOfMass.X(), (4000.0 * 10.0 - hole * 5.0) / (4000.0 - hole), 1e-6);
    EXPECT_NEAR(p.centerOfMass.Y(), 10.0, 1e-6);
    EXPECT_NEAR(p.boxMin.Distance(gp_Pnt(0.0, 0.0, 0.0)), 0.0, 1e-6);
    EXPECT_NEAR(p.boxMax.Distance(gp_Pnt(20.0, 20.0, 10.0)), 0.0, 1e-6);
}

} // namespace
