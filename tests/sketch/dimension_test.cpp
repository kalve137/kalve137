// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/dimension.h"

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

} // namespace
} // namespace kalve137::sketch
