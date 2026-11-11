// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/profiles.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>

using namespace kalve137::sketch;

namespace {

const gp_Ax3 kPlane;

struct Rectangle {
    std::array<Id, 4> corners{};
    std::array<Id, 4> sides{};
};

Rectangle addRectangle(Sketch& s, Vec2 low, Vec2 high)
{
    Rectangle r;
    r.corners = {s.addPoint(low), s.addPoint({high.x, low.y}), s.addPoint(high), s.addPoint({low.x, high.y})};
    for (int i = 0; i < 4; ++i) {
        r.sides[i] = s.addLine(r.corners[i], r.corners[(i + 1) % 4]);
    }
    return r;
}

void constrainRectangleShape(Sketch& s, const Rectangle& r)
{
    s.addConstraint(ConstraintKind::Horizontal, {r.sides[0]});
    s.addConstraint(ConstraintKind::Vertical, {r.sides[1]});
    s.addConstraint(ConstraintKind::Horizontal, {r.sides[2]});
    s.addConstraint(ConstraintKind::Vertical, {r.sides[3]});
}

} // namespace

TEST(Sketch, RemovingCurveDropsOrphanPointsAndConstraints)
{
    Sketch s;
    const Rectangle r = addRectangle(s, {0, 0}, {10, 5});
    constrainRectangleShape(s, r);
    s.remove(r.sides[0]);
    EXPECT_EQ(s.curves().size(), 3u);
    EXPECT_EQ(s.points().size(), 5u);
    EXPECT_EQ(s.constraints().size(), 3u);
    s.remove(r.sides[1]);
    EXPECT_FALSE(s.isPoint(r.corners[1]));
}

TEST(Solver, DimensionsDriveRectangle)
{
    Sketch s;
    const Rectangle r = addRectangle(s, {1, 1}, {9, 4});
    constrainRectangleShape(s, r);
    s.addConstraint(ConstraintKind::Coincident, {r.corners[0], kOriginId});
    s.addConstraint(ConstraintKind::Distance, {r.sides[0]}, 40.0);
    s.addConstraint(ConstraintKind::Distance, {r.sides[1]}, 20.0);

    const SolveResult result = solve(s);
    ASSERT_TRUE(result.solved);
    EXPECT_EQ(result.dof, 0);
    EXPECT_TRUE(result.isFullyConstrained());
    EXPECT_TRUE(result.fullyConstrained.contains(r.sides[2]));
    const Vec2 far = s.point(r.corners[2])->pos;
    EXPECT_NEAR(far.x, 40.0, 1e-6);
    EXPECT_NEAR(far.y, 20.0, 1e-6);
}

TEST(Solver, RejectsNonFiniteResults)
{
    Sketch s;
    const Id a = s.addPoint({0, 0});
    const Id b = s.addPoint({10, 0});
    const Id line = s.addLine(a, b);
    s.addConstraint(ConstraintKind::Horizontal, {line});
    s.addPoint({std::nan(""), 0});

    const SolveResult result = solve(s);
    EXPECT_FALSE(result.solved);
    EXPECT_DOUBLE_EQ(s.point(b)->pos.x, 10.0);
    EXPECT_DOUBLE_EQ(s.point(b)->pos.y, 0.0);
}

TEST(Sketch, ValidateAcceptsBuiltSketchesAndNamesProblems)
{
    Sketch s;
    const Rectangle r = addRectangle(s, {0, 0}, {10, 5});
    constrainRectangleShape(s, r);
    s.addArc(s.addPoint({20, 0}), s.addPoint({25, 0}), s.addPoint({20, 5}));
    EXPECT_FALSE(validate(s).has_value());

    Sketch dangling = s;
    dangling.insertCurve(500, {CurveKind::Line, {r.corners[0], 999}, 0.0, false, false});
    ASSERT_TRUE(validate(dangling).has_value());
    EXPECT_NE(validate(dangling)->find("999"), std::string::npos);

    Sketch noValue = s;
    noValue.addConstraint(ConstraintKind::Distance, {r.sides[0]}, std::numeric_limits<double>::infinity());
    EXPECT_TRUE(validate(noValue).has_value());
}

TEST(Solver, ReportsRemainingFreedom)
{
    Sketch s;
    const Rectangle r = addRectangle(s, {0, 0}, {10, 5});
    constrainRectangleShape(s, r);
    const SolveResult result = solve(s);
    ASSERT_TRUE(result.solved);
    EXPECT_EQ(result.dof, 4);
    EXPECT_FALSE(result.fullyConstrained.contains(r.sides[0]));
}

TEST(Solver, UnconstrainedCircleIsFree)
{
    Sketch s;
    const Id center = s.addPoint({15, 15});
    const Id circle = s.addCircle(center, 5.0);
    const SolveResult result = solve(s);
    ASSERT_TRUE(result.solved);
    EXPECT_EQ(result.dof, 3);
    EXPECT_FALSE(result.fullyConstrained.contains(center));
    EXPECT_FALSE(result.fullyConstrained.contains(circle));
}

TEST(Solver, FlagsConflictingDimensions)
{
    Sketch s;
    const Id a = s.addPoint({0, 0});
    const Id b = s.addPoint({10, 0});
    const Id line = s.addLine(a, b);
    s.addConstraint(ConstraintKind::Distance, {line}, 10.0);
    const Id second = s.addConstraint(ConstraintKind::Distance, {a, b}, 20.0);
    const SolveResult result = solve(s);
    EXPECT_FALSE(result.solved);
    EXPECT_FALSE(result.conflicting.empty());
    EXPECT_NE(std::ranges::find(result.conflicting, second), result.conflicting.end());
    EXPECT_NEAR(s.point(b)->pos.x, 10.0, 1e-9);
}

TEST(Solver, CircleRadiusAndTangentLine)
{
    Sketch s;
    const Id center = s.addPoint({0, 0});
    s.addConstraint(ConstraintKind::Coincident, {center, kOriginId});
    const Id circle = s.addCircle(center, 3.0);
    s.addConstraint(ConstraintKind::Diameter, {circle}, 20.0);
    const Id a = s.addPoint({-20, 12});
    const Id b = s.addPoint({20, 12});
    const Id line = s.addLine(a, b);
    s.addConstraint(ConstraintKind::Horizontal, {line});
    s.addConstraint(ConstraintKind::Tangent, {line, circle});

    const SolveResult result = solve(s);
    ASSERT_TRUE(result.solved);
    EXPECT_NEAR(s.curve(circle)->radius, 10.0, 1e-6);
    EXPECT_NEAR(std::abs(s.point(a)->pos.y), 10.0, 1e-6);
}

TEST(Solver, ArcKeepsEndpointsOnCircle)
{
    Sketch s;
    const Id center = s.addPoint({0, 0});
    const Id start = s.addPoint({5, 0});
    const Id end = s.addPoint({0, 5});
    const Id arc = s.addArc(center, start, end);
    s.addConstraint(ConstraintKind::Radius, {arc}, 8.0);
    const SolveResult result = solve(s);
    ASSERT_TRUE(result.solved);
    const Vec2 c = s.point(center)->pos;
    EXPECT_NEAR(distance(c, s.point(start)->pos), 8.0, 1e-6);
    EXPECT_NEAR(distance(c, s.point(end)->pos), 8.0, 1e-6);
}

TEST(Profiles, RectangleIsOneProfile)
{
    Sketch s;
    addRectangle(s, {0, 0}, {40, 20});
    const auto profiles = findProfiles(s, kPlane);
    ASSERT_EQ(profiles.size(), 1u);
    EXPECT_TRUE(profileAt(profiles, kPlane, {20, 10}).has_value());
    EXPECT_FALSE(profileAt(profiles, kPlane, {50, 10}).has_value());
    const auto inside = interiorPoint(profiles[0], kPlane);
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(profileAt(profiles, kPlane, *inside), 0u);
}

TEST(Profiles, HoleMakesTwoProfiles)
{
    Sketch s;
    addRectangle(s, {0, 0}, {40, 20});
    s.addCircle(s.addPoint({20, 10}), 5.0);
    const auto profiles = findProfiles(s, kPlane);
    ASSERT_EQ(profiles.size(), 2u);
    const auto ring = profileAt(profiles, kPlane, {2, 2});
    const auto disc = profileAt(profiles, kPlane, {20, 10});
    ASSERT_TRUE(ring && disc);
    EXPECT_NE(*ring, *disc);
}

TEST(Profiles, OverlappingCirclesSplitIntoThree)
{
    Sketch s;
    s.addCircle(s.addPoint({0, 0}), 10.0);
    s.addCircle(s.addPoint({12, 0}), 10.0);
    EXPECT_EQ(findProfiles(s, kPlane).size(), 3u);
}

TEST(Profiles, ConstructionAndOpenCurvesAreIgnored)
{
    Sketch s;
    const Rectangle r = addRectangle(s, {0, 0}, {10, 10});
    s.curve(r.sides[0])->construction = true;
    s.addLine(s.addPoint({20, 0}), s.addPoint({30, 0}));
    EXPECT_TRUE(findProfiles(s, kPlane).empty());
}
