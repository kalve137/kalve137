// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/dimension.h"
#include "sketch/dimension_layout.h"

#include <gtest/gtest.h>

namespace kalve137::sketch {
namespace {

TEST(DimensionPlacement, PointToLineMeasuresToTheFoot)
{
    Sketch s;
    const Id line = s.addLine(s.addPoint({0.0, 0.0}), s.addPoint({10.0, 0.0}));
    const Id point = s.addPoint({4.0, 6.0});
    const Constraint constraint{ConstraintKind::Distance, {point, line}, 6.0};
    const auto ends = measuredPoints(s, constraint);
    ASSERT_TRUE(ends);
    EXPECT_NEAR(ends->second.x, 4.0, 1e-12);
    EXPECT_NEAR(ends->second.y, 0.0, 1e-12);
    const std::optional<Vec2> anchor = dimensionAnchor(s, constraint);
    ASSERT_TRUE(anchor);
    EXPECT_NEAR(anchor->y, 3.0, 1e-12);
}

TEST(DimensionPlacement, AnglesAnchorAtTheCornerAndCirclesAtTheCenter)
{
    Sketch s;
    const Id corner = s.addPoint({2.0, 3.0});
    const Id first = s.addLine(s.addPoint({7.0, 3.0}), corner);
    const Id second = s.addLine(s.addPoint({2.0, 9.0}), s.addPoint({2.0, 5.0}));
    const std::optional<Vec2> angle = dimensionAnchor(s, {ConstraintKind::Angle, {first, second}, 90.0});
    ASSERT_TRUE(angle);
    EXPECT_NEAR(angle->x, 2.0, 1e-12);
    EXPECT_NEAR(angle->y, 3.0, 1e-12);

    const Id circle = s.addCircle(s.addPoint({-4.0, 1.0}), 3.0);
    const std::optional<Vec2> center = dimensionAnchor(s, {ConstraintKind::Diameter, {circle}, 6.0});
    ASSERT_TRUE(center);
    EXPECT_NEAR(center->x, -4.0, 1e-12);

    const Id parallel = s.addLine(s.addPoint({0.0, 4.0}), s.addPoint({5.0, 4.0}));
    EXPECT_FALSE(dimensionAnchor(s, {ConstraintKind::Angle, {first, parallel}, 0.0}));
}

TEST(DimensionPlacement, LabelFollowsTheAnchorWhenGeometryMoves)
{
    Sketch s;
    const Id end = s.addPoint({10.0, 0.0});
    const Id line = s.addLine(s.addPoint({0.0, 0.0}), end);
    Constraint constraint{ConstraintKind::Distance, {line}, 10.0};
    EXPECT_FALSE(labelPosition(s, constraint));
    constraint.labelOffset = Vec2{1.0, -4.0};
    s.point(end)->pos = {20.0, 0.0};
    const std::optional<Vec2> label = labelPosition(s, constraint);
    ASSERT_TRUE(label);
    EXPECT_NEAR(label->x, 11.0, 1e-12);
    EXPECT_NEAR(label->y, -4.0, 1e-12);
}

TEST(DimensionPlacement, CursorPicksHorizontalVerticalOrAligned)
{
    const Vec2 a{0.0, 0.0};
    const Vec2 b{10.0, 5.0};
    EXPECT_EQ(pointDistanceKind(a, b, {5.0, 12.0}), ConstraintKind::HorizontalDistance);
    EXPECT_EQ(pointDistanceKind(a, b, {5.0, -3.0}), ConstraintKind::HorizontalDistance);
    EXPECT_EQ(pointDistanceKind(a, b, {-6.0, 2.0}), ConstraintKind::VerticalDistance);
    EXPECT_EQ(pointDistanceKind(a, b, {4.0, 3.0}), ConstraintKind::Distance);
    EXPECT_EQ(pointDistanceKind(a, b, {14.0, 9.0}), ConstraintKind::Distance);
}

TEST(LineCorner, ShortPerpendicularSegmentsMeet)
{
    const std::optional<Vec2> corner = lineCorner({0.0, 0.0}, {1e-7, 0.0}, {2e-8, -5e-8}, {2e-8, 5e-8});
    ASSERT_TRUE(corner);
    EXPECT_NEAR(corner->x, 2e-8, 1e-18);
    EXPECT_NEAR(corner->y, 0.0, 1e-18);
}

TEST(LineCorner, LongNearlyParallelSegmentsDoNotMeet)
{
    const Vec2 from{0.0, 0.0};
    const Vec2 to{1e6, 0.0};
    EXPECT_FALSE(lineCorner(from, to, {0.0, 1.0}, {1e6, 1.0}));
    EXPECT_FALSE(lineCorner(from, to, {0.0, 1.0}, {1e6, 1.0 + 1e-6}));
    EXPECT_TRUE(lineCorner(from, to, {0.0, 1.0}, {1e6, 1.0 + 1e-1}));
}

TEST(LineCorner, ShortNearlyParallelSegmentsDoNotMeet)
{
    EXPECT_FALSE(lineCorner({0.0, 0.0}, {1e-6, 0.0}, {0.0, 1e-6}, {1e-6, 1e-6}));
    EXPECT_TRUE(lineCorner({0.0, 0.0}, {1e-6, 0.0}, {0.0, 1e-6}, {1e-6, 2e-6}));
}

TEST(LineCorner, ZeroLengthSegmentHasNoCorner)
{
    EXPECT_FALSE(lineCorner({1.0, 1.0}, {1.0, 1.0}, {0.0, 0.0}, {0.0, 5.0}));
    EXPECT_FALSE(lineCorner({0.0, 0.0}, {5.0, 0.0}, {2.0, 2.0}, {2.0, 2.0}));
}

TEST(DimensionPlacement, AngleLayoutSkipsParallelLongAndMeetsShortSegments)
{
    Sketch longSketch;
    const Id first = longSketch.addLine(longSketch.addPoint({0.0, 0.0}), longSketch.addPoint({1e6, 0.0}));
    const Id second = longSketch.addLine(longSketch.addPoint({0.0, 1.0}), longSketch.addPoint({1e6, 1.0 + 1e-6}));
    longSketch.addConstraint(ConstraintKind::Angle, {first, second}, 0.0);
    EXPECT_TRUE(layoutDimensions(longSketch, 1.0).empty());

    Sketch shortSketch;
    const Id corner = shortSketch.addPoint({0.0, 0.0});
    const Id a = shortSketch.addLine(corner, shortSketch.addPoint({1e-7, 0.0}));
    const Id b = shortSketch.addLine(corner, shortSketch.addPoint({0.0, 1e-7}));
    const Id angle = shortSketch.addConstraint(ConstraintKind::Angle, {a, b}, 90.0);
    EXPECT_EQ(layoutDimensions(shortSketch, 1e-9).count(angle), 1u);
}

} // namespace
} // namespace kalve137::sketch
