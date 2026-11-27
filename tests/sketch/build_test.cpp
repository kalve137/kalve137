// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/build.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace kalve137::sketch;

TEST(CircleThrough, FindsCenterAndRejectsCollinear)
{
    const std::optional<Vec2> c = circleThrough({10, 0}, {0, 10}, {-10, 0});
    ASSERT_TRUE(c);
    EXPECT_NEAR(c->x, 0.0, 1e-9);
    EXPECT_NEAR(c->y, 0.0, 1e-9);
    EXPECT_FALSE(circleThrough({0, 0}, {1, 1}, {2, 2}));
}

TEST(CenterRectangle, KeepsCenterAndLeavesSizeFree)
{
    Sketch s;
    const RectangleSides sides = addCenterRectangle(s, kOriginId, {30, 20});
    const SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, 2);
    const Curve* bottom = s.curve(sides.bottom);
    EXPECT_NEAR(s.point(bottom->points[0])->pos.x, -30.0, 1e-9);
    EXPECT_NEAR(s.point(bottom->points[0])->pos.y, -20.0, 1e-9);
}

TEST(Polygon, HexagonHasEqualSidesAndFourFreedoms)
{
    Sketch s;
    const Id center = s.addPoint({5, 5});
    const Polygon hexagon = addPolygon(s, center, {15, 5}, 6);
    ASSERT_EQ(hexagon.sides.size(), 6u);
    const SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, 4);
    for (Id side : hexagon.sides) {
        const Curve* line = s.curve(side);
        EXPECT_NEAR(distance(s.point(line->points[0])->pos, s.point(line->points[1])->pos), 10.0, 1e-6);
    }
}

namespace {

double lineDistance(const Sketch& s, Vec2 p, Id line)
{
    const Curve* curve = s.curve(line);
    const Vec2 a = s.point(curve->points[0])->pos;
    const Vec2 b = s.point(curve->points[1])->pos;
    return std::abs((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / distance(a, b);
}

void expectSlot(const Sketch& s, const Slot& slot, double width)
{
    const Curve* axis = s.curve(slot.centerLine);
    for (Id arc : slot.arcs) {
        EXPECT_NEAR(s.curve(arc)->radius, width / 2.0, 1e-6);
    }
    EXPECT_EQ(s.curve(slot.arcs[0])->points[0], axis->points[1]);
    EXPECT_EQ(s.curve(slot.arcs[1])->points[0], axis->points[0]);
    for (Id side : slot.sides) {
        for (Id center : axis->points) {
            EXPECT_NEAR(lineDistance(s, s.point(center)->pos, side), width / 2.0, 1e-6);
        }
    }
}

} // namespace

TEST(Slot, KeepsTangentSidesAndFiveFreedoms)
{
    Sketch s;
    const Id start = s.addPoint({-10, 5});
    const Id end = s.addPoint({14, 23});
    const Slot slot = addSlot(s, start, end, 8.0);
    ASSERT_EQ(slot.sides.size(), 2u);
    ASSERT_EQ(slot.arcs.size(), 2u);
    EXPECT_TRUE(s.curve(slot.centerLine)->construction);
    const SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, 5);
    expectSlot(s, slot, 8.0);
}

TEST(Slot, StaysASlotWhenItsDimensionsChange)
{
    Sketch s;
    const Id start = s.addPoint({0, 0}, true);
    const Id end = s.addPoint({17, 10});
    const Slot slot = addSlot(s, start, end, 6.0);
    const Id width = s.addConstraint(ConstraintKind::Distance, {s.curve(slot.sides[0])->points[0], slot.sides[1]}, 6.0);
    const Id length = s.addConstraint(ConstraintKind::Distance, {slot.centerLine}, 20.0);
    s.addConstraint(ConstraintKind::Angle, {slot.centerLine, s.addLine(start, s.addPoint({10, 0}, true), true)}, 30.0);
    SolveResult result = solve(s);
    ASSERT_TRUE(result.solved);
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, 0);

    s.constraint(width)->value = 11.0;
    s.constraint(length)->value = 35.0;
    result = solve(s);
    ASSERT_TRUE(result.solved);
    expectSlot(s, slot, 11.0);
    const Vec2 e = s.point(end)->pos;
    EXPECT_NEAR(distance({0, 0}, e), 35.0, 1e-6);
    EXPECT_NEAR(std::atan2(e.y, e.x) * 180.0 / 3.14159265358979, 30.0, 1e-6);
}

TEST(Slot, TakesALengthAndAWidthOnFreeCenters)
{
    Sketch s;
    const Slot slot = addSlot(s, s.addPoint({-20, 20}), s.addPoint({10, 20}), 8.0);
    s.addConstraint(ConstraintKind::Distance, {s.curve(slot.sides[0])->points[0], slot.sides[1]}, 8.0);
    s.addConstraint(ConstraintKind::Distance, {slot.centerLine}, 30.0);
    const SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.conflicting.empty());
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, 3);
}

TEST(Slot, CenterPointSlotKeepsItsMiddleOnTheCenter)
{
    Sketch s;
    const Slot slot = addSlot(s, s.addPoint({-20, 0}), s.addPoint({20, 0}), 20.0);
    s.addConstraint(ConstraintKind::Midpoint, {kOriginId, slot.centerLine});
    s.addConstraint(ConstraintKind::Distance, {s.curve(slot.sides[0])->points[0], slot.sides[1]}, 20.0);
    const Id length = s.addConstraint(ConstraintKind::Distance, {slot.centerLine}, 40.0);
    SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.conflicting.empty());
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, 1);

    s.constraint(length)->value = 60.0;
    result = solve(s);
    ASSERT_TRUE(result.solved);
    const Curve* axis = s.curve(slot.centerLine);
    const Vec2 a = s.point(axis->points[0])->pos;
    const Vec2 b = s.point(axis->points[1])->pos;
    EXPECT_NEAR(distance(a, b), 60.0, 1e-6);
    EXPECT_NEAR(a.x + b.x, 0.0, 1e-6);
    EXPECT_NEAR(a.y + b.y, 0.0, 1e-6);
    expectSlot(s, slot, 20.0);
}

TEST(Slot, NeedsTwoCentersAndAWidth)
{
    Sketch s;
    const Id a = s.addPoint({0, 0});
    EXPECT_TRUE(addSlot(s, a, s.addPoint({0, 0}), 4.0).sides.empty());
    EXPECT_TRUE(addSlot(s, a, s.addPoint({10, 0}), 0.0).sides.empty());
}

TEST(ArcThrough, RunsThroughTheMiddlePoint)
{
    for (const double side : {1.0, -1.0}) {
        Sketch s;
        const Id a = s.addPoint({-10, 0});
        const Id b = s.addPoint({10, 0});
        const std::optional<Id> arc = addArcThrough(s, a, b, {0, 10 * side});
        ASSERT_TRUE(arc);
        const Curve* curve = s.curve(*arc);
        const Vec2 c = s.point(curve->points[0])->pos;
        EXPECT_NEAR(curve->radius, 10.0, 1e-9);
        const Vec2 start = s.point(curve->points[1])->pos;
        const Vec2 end = s.point(curve->points[2])->pos;
        const double sweep = std::remainder(
            std::atan2(end.y - c.y, end.x - c.x) - std::atan2(start.y - c.y, start.x - c.x), 2.0 * 3.14159265358979);
        EXPECT_NEAR(std::abs(sweep), 3.14159265358979, 1e-9);
        EXPECT_EQ(curve->points[1], side > 0 ? b : a);
    }
    Sketch s;
    EXPECT_FALSE(addArcThrough(s, s.addPoint({0, 0}), s.addPoint({10, 0}), {5, 0}));
}

namespace {

Id addYAxis(Sketch& s)
{
    const Id axis = s.addLine(kOriginId, s.addPoint({0, 40}), true);
    s.addConstraint(ConstraintKind::Vertical, {axis});
    return axis;
}

} // namespace

TEST(Mirror, CopyFollowsTheSourceWithoutNewFreedoms)
{
    Sketch s;
    const Id axis = addYAxis(s);
    const Id line = s.addLine(s.addPoint({10, 5}), s.addPoint({20, 15}));
    const Id circle = s.addCircle(s.addPoint({15, 30}), 4.0);
    const int before = solve(s).dof;

    const std::optional<std::vector<Id>> copies = addMirror(s, {line, circle, axis}, axis);
    ASSERT_TRUE(copies);
    ASSERT_EQ(copies->size(), 2u);
    SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.redundant.empty());
    EXPECT_EQ(result.dof, before);

    s.point(s.curve(line)->points[1])->pos = {25, 18};
    s.addConstraint(ConstraintKind::Radius, {circle}, 6.0);
    result = solve(s);
    ASSERT_TRUE(result.solved);
    const Vec2 copyEnd = s.point(s.curve(copies->at(0))->points[1])->pos;
    const Vec2 sourceEnd = s.point(s.curve(line)->points[1])->pos;
    EXPECT_NEAR(copyEnd.x, -sourceEnd.x, 1e-6);
    EXPECT_NEAR(copyEnd.y, sourceEnd.y, 1e-6);
    EXPECT_NEAR(s.curve(copies->at(1))->radius, 6.0, 1e-6);
}

TEST(Mirror, SharesPointsOnTheAxisAndSwapsArcEnds)
{
    Sketch s;
    const Id axis = addYAxis(s);
    const Id top = s.addPoint({0, 20});
    const Id arc = s.addArc(s.addPoint({0, 10}), s.addPoint({10, 10}), top);
    const Id spoke = s.addLine(kOriginId, s.addPoint({10, 0}));
    const std::size_t pointsBefore = s.points().size();

    const std::optional<std::vector<Id>> copies = addMirror(s, {arc, spoke}, axis);
    ASSERT_TRUE(copies);
    ASSERT_EQ(copies->size(), 2u);
    EXPECT_EQ(s.points().size(), pointsBefore + 2);
    const Curve* copiedArc = s.curve(copies->at(0));
    EXPECT_EQ(copiedArc->points[1], top);
    EXPECT_NEAR(s.point(copiedArc->points[2])->pos.x, -10.0, 1e-9);
    EXPECT_EQ(s.curve(copies->at(1))->points[0], kOriginId);
    const SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.redundant.empty());
}

TEST(Mirror, NeedsALineAxisAndSomethingToCopy)
{
    Sketch s;
    const Id axis = addYAxis(s);
    const Id circle = s.addCircle(s.addPoint({10, 0}), 3.0);
    EXPECT_FALSE(addMirror(s, {axis}, axis));
    EXPECT_FALSE(addMirror(s, {axis}, circle));
    const Id along = s.addLine(s.addPoint({0, 5}), s.addPoint({0, 15}));
    EXPECT_FALSE(addMirror(s, {along}, axis));
}
