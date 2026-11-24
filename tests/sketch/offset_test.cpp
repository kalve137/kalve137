// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/build.h"
#include "sketch/edit.h"
#include "sketch/offset.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <vector>

using namespace kalve137::sketch;

namespace {

constexpr double kNear = 1e-6;

struct Box {
    double left = std::numeric_limits<double>::max();
    double bottom = std::numeric_limits<double>::max();
    double right = std::numeric_limits<double>::lowest();
    double top = std::numeric_limits<double>::lowest();
};

Box boundsOf(const Sketch& s, const std::vector<Id>& curves)
{
    Box box;
    for (const Id id : curves) {
        for (const Id point : s.curve(id)->points) {
            const Vec2 p = s.point(point)->pos;
            box = {std::min(box.left, p.x), std::min(box.bottom, p.y), std::max(box.right, p.x),
                   std::max(box.top, p.y)};
        }
    }
    return box;
}

void expectBox(const Box& box, double left, double bottom, double right, double top)
{
    EXPECT_NEAR(box.left, left, kNear);
    EXPECT_NEAR(box.bottom, bottom, kNear);
    EXPECT_NEAR(box.right, right, kNear);
    EXPECT_NEAR(box.top, top, kNear);
}

struct Rectangle {
    std::vector<Id> sides;
    Id width = 0;
};

Rectangle fixedRectangle(Sketch& s)
{
    const Id a = s.addPoint({0, 0}, true);
    const Id b = s.addPoint({40, 0});
    const Id c = s.addPoint({40, 20});
    const Id d = s.addPoint({0, 20});
    Rectangle r;
    r.sides = {s.addLine(a, b), s.addLine(b, c), s.addLine(c, d), s.addLine(d, a)};
    s.addConstraint(ConstraintKind::Horizontal, {r.sides[0]});
    s.addConstraint(ConstraintKind::Vertical, {r.sides[1]});
    s.addConstraint(ConstraintKind::Horizontal, {r.sides[2]});
    s.addConstraint(ConstraintKind::Vertical, {r.sides[3]});
    r.width = s.addConstraint(ConstraintKind::Distance, {r.sides[0]}, 40.0);
    s.addConstraint(ConstraintKind::Distance, {r.sides[1]}, 20.0);
    return r;
}

void expectOnlyTheOffset(const Sketch& before, const Sketch& s, const Offset& offset, std::size_t pairs)
{
    EXPECT_EQ(s.curves().size(), before.curves().size() + offset.curves.size());
    for (const Id id : offset.curves) {
        EXPECT_FALSE(s.curve(id)->construction) << id;
    }
    EXPECT_EQ(s.constraints().size(), before.constraints().size() + 1);
    const Constraint* constraint = s.constraint(offset.constraint);
    ASSERT_NE(constraint, nullptr);
    EXPECT_EQ(constraint->kind, ConstraintKind::Offset);
    EXPECT_EQ(offsetPairs(*constraint).size(), pairs);
    Sketch without = before;
    Sketch with = s;
    EXPECT_EQ(solve(with).dof, solve(without).dof);
}

double leftDistance(const Sketch& s, Id line, Vec2 p)
{
    const Curve* curve = s.curve(line);
    const Vec2 a = s.point(curve->points[0])->pos;
    const Vec2 d = s.point(curve->points[1])->pos - a;
    const Vec2 w = p - a;
    return (d.x * w.y - d.y * w.x) / distance(a, a + d);
}

void expectClean(Sketch& s, std::optional<int> dof)
{
    const SolveResult result = solve(s);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.conflicting.empty());
    EXPECT_TRUE(result.redundant.empty());
    if (dof) {
        EXPECT_EQ(result.dof, *dof);
    }
}

} // namespace

TEST(Offset, RectangleGrowsOutwardAndFollowsItsSourceAndDistance)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    const Sketch before = s;
    const std::optional<Offset> offset = addOffset(s, rectangle.sides, 5.0);
    ASSERT_TRUE(offset);
    ASSERT_EQ(offset->curves.size(), 4U);
    ASSERT_NE(offset->constraint, 0);
    expectClean(s, 0);
    expectOnlyTheOffset(before, s, *offset, 4);
    expectBox(boundsOf(s, offset->curves), -5, -5, 45, 25);

    s.constraint(offset->constraint)->value = 8.0;
    s.constraint(rectangle.width)->value = 60.0;
    ASSERT_TRUE(solve(s).solved);
    expectBox(boundsOf(s, offset->curves), -8, -8, 68, 28);
}

TEST(Offset, NegativeDistanceGoesInsideUntilItNoLongerFits)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    Sketch inside = s;
    const std::optional<Offset> offset = addOffset(inside, rectangle.sides, -5.0);
    ASSERT_TRUE(offset);
    expectClean(inside, 0);
    expectOnlyTheOffset(s, inside, *offset, 4);
    expectBox(boundsOf(inside, offset->curves), 5, 5, 35, 15);
    EXPECT_FALSE(addOffset(s, rectangle.sides, -12.0));
}

TEST(Offset, OpenChainKeepsItsEndsBesideTheSourceEnds)
{
    Sketch s;
    const Id a = s.addPoint({0, 0}, true);
    const Id b = s.addPoint({30, 0}, true);
    const Id c = s.addPoint({30, 20}, true);
    const std::vector<Id> lines{s.addLine(a, b), s.addLine(b, c)};
    const Sketch before = s;
    const std::optional<Offset> offset = addOffset(s, lines, 4.0);
    ASSERT_TRUE(offset);
    expectClean(s, 0);
    expectOnlyTheOffset(before, s, *offset, 2);
    expectBox(boundsOf(s, offset->curves), 0, 4, 26, 20);

    s.constraint(offset->constraint)->value = 6.0;
    ASSERT_TRUE(solve(s).solved);
    expectBox(boundsOf(s, offset->curves), 0, 6, 24, 20);
}

TEST(Offset, SlotGrowsIntoALargerSlot)
{
    Sketch s;
    const Slot slot = addSlot(s, s.addPoint({-10, 0}, true), s.addPoint({10, 0}, true), 10.0);
    s.addConstraint(ConstraintKind::Distance, {s.curve(slot.sides[0])->points[0], slot.sides[1]}, 10.0);
    const std::vector<Id> chain = chainThrough(s, slot.sides[0]);
    ASSERT_EQ(chain.size(), 4U);
    const Sketch before = s;
    const std::optional<Offset> offset = addOffset(s, chain, 3.0);
    ASSERT_TRUE(offset);
    ASSERT_NE(offset->constraint, 0);
    expectClean(s, 0);
    expectOnlyTheOffset(before, s, *offset, 4);
    expectBox(boundsOf(s, offset->curves), -10, -8, 10, 8);

    s.constraint(offset->constraint)->value = 1.0;
    ASSERT_TRUE(solve(s).solved);
    for (const Id id : offset->curves) {
        if (s.curve(id)->kind == CurveKind::Arc) {
            EXPECT_NEAR(s.curve(id)->radius, 6.0, kNear);
        }
    }
    expectBox(boundsOf(s, offset->curves), -10, -6, 10, 6);
}

TEST(Offset, RoundedCornerKeepsItsArcTangent)
{
    Sketch s;
    const Id corner = s.addPoint({20, 0}, true);
    const Id turn = s.addPoint({25, 5}, true);
    const Id start = s.addLine(s.addPoint({0, 0}, true), corner);
    const Id arc = s.addArc(s.addPoint({20, 5}, true), corner, turn);
    const Id end = s.addLine(turn, s.addPoint({25, 25}, true));
    const Sketch before = s;
    const std::optional<Offset> offset = addOffset(s, {end, arc, start}, 2.0);
    ASSERT_TRUE(offset);
    expectClean(s, std::nullopt);
    expectOnlyTheOffset(before, s, *offset, 3);
    expectBox(boundsOf(s, offset->curves), 0, 2, 23, 25);

    s.constraint(offset->constraint)->value = 4.0;
    ASSERT_TRUE(solve(s).solved);
    expectBox(boundsOf(s, offset->curves), 0, 4, 21, 25);
    for (const Id id : offset->curves) {
        if (s.curve(id)->kind == CurveKind::Arc) {
            EXPECT_NEAR(s.curve(id)->radius, 1.0, kNear);
        }
    }
}

TEST(Offset, CircleStaysConcentric)
{
    Sketch s;
    const Id center = s.addPoint({5, 5}, true);
    const Id circle = s.addCircle(center, 10.0);
    s.addConstraint(ConstraintKind::Diameter, {circle}, 20.0);
    const Sketch before = s;
    const std::optional<Offset> offset = addOffset(s, {circle}, 2.0);
    ASSERT_TRUE(offset);
    ASSERT_EQ(offset->curves.size(), 1U);
    EXPECT_EQ(s.curve(offset->curves[0])->points[0], center);
    expectClean(s, 0);
    expectOnlyTheOffset(before, s, *offset, 1);

    s.constraint(offset->constraint)->value = 5.0;
    ASSERT_TRUE(solve(s).solved);
    EXPECT_NEAR(s.curve(offset->curves[0])->radius, 15.0, kNear);
}

TEST(Offset, DistanceEditMovesTheOffsetAndLeavesAFreeSource)
{
    Sketch s;
    const Id a = s.addPoint({0, 0});
    const Id b = s.addPoint({40, 0});
    const Id c = s.addPoint({40, 20});
    const Id d = s.addPoint({0, 20});
    const std::vector<Id> sides{s.addLine(a, b), s.addLine(b, c), s.addLine(c, d), s.addLine(d, a)};
    s.addConstraint(ConstraintKind::Horizontal, {sides[0]});
    s.addConstraint(ConstraintKind::Vertical, {sides[1]});
    s.addConstraint(ConstraintKind::Horizontal, {sides[2]});
    s.addConstraint(ConstraintKind::Vertical, {sides[3]});
    const std::optional<Offset> offset = addOffset(s, sides, 5.0);
    ASSERT_TRUE(offset);
    s.constraint(offset->constraint)->value = 8.0;
    ASSERT_TRUE(solve(s).solved);
    expectBox(boundsOf(s, sides), 0, 0, 40, 20);
    expectBox(boundsOf(s, offset->curves), -8, -8, 48, 28);

    ASSERT_TRUE(solve(s, {{c, {60, 30}}}).solved);
    expectBox(boundsOf(s, sides), 0, 0, 60, 30);
    expectBox(boundsOf(s, offset->curves), -8, -8, 68, 38);
}

TEST(Offset, LineSideHoldsWhenTheSourceSwingsFarAway)
{
    Sketch base;
    const Id a = base.addPoint({0, 0}, true);
    const Id b = base.addPoint({40, 0});
    const Id source = base.addLine(a, b);
    const std::optional<Offset> offset = addOffset(base, {source}, 5.0);
    ASSERT_TRUE(offset);
    expectClean(base, std::nullopt);
    const Id copy = offset->curves[0];
    for (const Vec2 target : {Vec2{400, 0}, Vec2{40, 300}, Vec2{-50, -80}, Vec2{5, -400}, Vec2{-40, 1}}) {
        Sketch s = base;
        ASSERT_TRUE(solve(s, {{b, target}}).solved);
        for (const Id end : s.curve(copy)->points) {
            EXPECT_NEAR(leftDistance(s, source, s.point(end)->pos), 5.0, kNear);
        }
        EXPECT_NEAR(distance(s.point(s.curve(copy)->points[0])->pos, s.point(a)->pos), 5.0, kNear);
    }
}

TEST(Offset, RoundSidesHoldWhenTheSourceGrowsOrShrinks)
{
    Sketch s;
    const Id circle = s.addCircle(s.addPoint({0, 0}, true), 10.0);
    const Id diameter = s.addConstraint(ConstraintKind::Diameter, {circle}, 20.0);
    const std::optional<Offset> inside = addOffset(s, {circle}, -3.0);
    ASSERT_TRUE(inside);
    const Id center = s.addPoint({50, 0}, true);
    const Id from = s.addPoint({60, 0});
    const Id to = s.addPoint({50, 10});
    const Id arc = s.addArc(center, from, to);
    const Id radius = s.addConstraint(ConstraintKind::Radius, {arc}, 10.0);
    const std::optional<Offset> outside = addOffset(s, {arc}, -2.0);
    ASSERT_TRUE(outside);
    EXPECT_NEAR(s.curve(outside->curves[0])->radius, 12.0, kNear);
    expectClean(s, std::nullopt);

    s.constraint(diameter)->value = 400.0;
    s.constraint(radius)->value = 3.0;
    ASSERT_TRUE(solve(s).solved);
    EXPECT_NEAR(s.curve(inside->curves[0])->radius, 197.0, kNear);
    EXPECT_NEAR(s.curve(outside->curves[0])->radius, 5.0, kNear);
}

TEST(Offset, ASourceShrunkBelowTheInwardDistanceFailsAndStillLoads)
{
    Sketch base;
    const Id circle = base.addCircle(base.addPoint({0, 0}, true), 10.0);
    const Id diameter = base.addConstraint(ConstraintKind::Diameter, {circle}, 20.0);
    const std::optional<Offset> inside = addOffset(base, {circle}, -3.0);
    ASSERT_TRUE(inside);
    expectClean(base, std::nullopt);
    for (const double shrunk : {6.0, 4.0}) {
        Sketch s = base;
        s.constraint(diameter)->value = shrunk;
        EXPECT_FALSE(solve(s).solved) << shrunk;
        EXPECT_NEAR(s.curve(inside->curves[0])->radius, 7.0, kNear);
        EXPECT_FALSE(validate(s)) << shrunk;
    }
    for (const double reaching : {10.0, 12.0}) {
        Sketch s = base;
        s.constraint(inside->constraint)->value = reaching;
        EXPECT_FALSE(solve(s).solved) << reaching;
        EXPECT_FALSE(validate(s)) << reaching;
    }
    EXPECT_FALSE(addOffset(base, {circle}, -10.0));

    Sketch flat = base;
    flat.curve(inside->curves[0])->radius = 0.0;
    EXPECT_TRUE(validate(flat));
}

TEST(Offset, InwardLinesPushedPastEachOtherFailAndStillLoad)
{
    Sketch base;
    const Rectangle rectangle = fixedRectangle(base);
    const std::optional<Offset> inside = addOffset(base, rectangle.sides, -5.0);
    ASSERT_TRUE(inside);
    expectBox(boundsOf(base, inside->curves), 5, 5, 35, 15);
    expectClean(base, std::nullopt);

    Sketch fits = base;
    fits.constraint(inside->constraint)->value = 9.0;
    ASSERT_TRUE(solve(fits).solved);
    expectBox(boundsOf(fits, inside->curves), 9, 9, 31, 11);

    for (const double crossed : {10.0, 12.0}) {
        Sketch s = base;
        s.constraint(inside->constraint)->value = crossed;
        EXPECT_FALSE(solve(s).solved) << crossed;
        expectBox(boundsOf(s, inside->curves), 5, 5, 35, 15);
        EXPECT_FALSE(validate(s)) << crossed;
    }
    Sketch narrowed = base;
    narrowed.constraint(rectangle.width)->value = 8.0;
    EXPECT_FALSE(solve(narrowed).solved);
    EXPECT_FALSE(validate(narrowed));
    EXPECT_FALSE(addOffset(base, rectangle.sides, -12.0));
}

TEST(Offset, DeletingCurvesDropsTheirPairsAndTheLastTakesTheConstraint)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    const std::optional<Offset> offset = addOffset(s, rectangle.sides, 5.0);
    ASSERT_TRUE(offset);

    std::map<Id, Id> copyOf;
    for (const OffsetPair& pair : offsetPairs(*s.constraint(offset->constraint))) {
        copyOf[pair.source] = pair.offset;
    }
    ASSERT_EQ(copyOf.size(), 4U);

    s.constraint(offset->constraint)->expression = "5";
    s.remove(copyOf.at(rectangle.sides[0]));
    ASSERT_NE(s.constraint(offset->constraint), nullptr);
    EXPECT_EQ(s.constraint(offset->constraint)->expression, "5");
    EXPECT_EQ(offsetPairs(*s.constraint(offset->constraint)).size(), 3U);
    EXPECT_EQ(s.constraint(offset->constraint)->joints.size(), 2U);
    EXPECT_FALSE(validate(s));
    expectClean(s, 0);
    expectBox(
        boundsOf(s, {copyOf.at(rectangle.sides[1]), copyOf.at(rectangle.sides[2]), copyOf.at(rectangle.sides[3])}), -5,
        0, 45, 25);

    s.remove(rectangle.sides[2]);
    ASSERT_NE(s.constraint(offset->constraint), nullptr);
    std::set<Id> kept;
    for (const OffsetPair& pair : offsetPairs(*s.constraint(offset->constraint))) {
        kept.insert(pair.source);
    }
    EXPECT_EQ(kept, (std::set<Id>{rectangle.sides[1], rectangle.sides[3]}));
    EXPECT_FALSE(validate(s));
    EXPECT_TRUE(solve(s).solved);

    s.remove(copyOf.at(rectangle.sides[1]));
    s.remove(copyOf.at(rectangle.sides[3]));
    EXPECT_EQ(s.constraint(offset->constraint), nullptr);
}

TEST(Offset, DeletingTheConstraintKeepsTheCurves)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    const std::optional<Offset> offset = addOffset(s, rectangle.sides, 5.0);
    ASSERT_TRUE(offset);
    s.remove(offset->constraint);
    EXPECT_EQ(s.constraint(offset->constraint), nullptr);
    for (const Id id : offset->curves) {
        EXPECT_NE(s.curve(id), nullptr);
    }
    EXPECT_TRUE(solve(s).solved);
}

TEST(Offset, FilletOnTheSourceDropsTheTouchedPairs)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    const std::optional<Offset> offset = addOffset(s, rectangle.sides, 5.0);
    ASSERT_TRUE(offset);
    const Id corner = s.curve(rectangle.sides[1])->points[1];
    const std::optional<Sketch> filleted = filletCorner(s, corner, 3.0);
    ASSERT_TRUE(filleted);
    const Constraint* constraint = filleted->constraint(offset->constraint);
    ASSERT_NE(constraint, nullptr);
    EXPECT_EQ(offsetPairs(*constraint).size(), 2U);
    EXPECT_FALSE(validate(*filleted));
}

TEST(Offset, ValidateRejectsDamagedOffsets)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    const std::optional<Offset> offset = addOffset(s, rectangle.sides, 5.0);
    ASSERT_TRUE(offset);
    EXPECT_FALSE(validate(s));
    const auto damaged = [&](const auto& damage) {
        Sketch copy = s;
        damage(*copy.constraint(offset->constraint));
        return validate(copy).has_value();
    };
    EXPECT_TRUE(damaged([](Constraint& c) { c.refs.pop_back(); }));
    EXPECT_TRUE(damaged([](Constraint& c) { c.sides.pop_back(); }));
    EXPECT_TRUE(damaged([](Constraint& c) { c.sides[0] = 0; }));
    EXPECT_TRUE(damaged([](Constraint& c) { c.value = 0.0; }));
    EXPECT_TRUE(damaged([&](Constraint& c) { c.refs[0] = s.curve(c.refs[0])->points[0]; }));
    EXPECT_TRUE(damaged([](Constraint& c) { c.refs[4] = c.refs[5]; }));
    EXPECT_TRUE(damaged([](Constraint& c) {
        c.kind = ConstraintKind::Parallel;
        c.refs = {c.refs[0], c.refs[1]};
    }));
    EXPECT_TRUE(damaged([](Constraint& c) {
        c.kind = ConstraintKind::Parallel;
        c.refs = {c.refs[0], c.refs[1]};
        c.sides.clear();
    }));
    EXPECT_TRUE(damaged([](Constraint& c) { c.joints.pop_back(); }));
    EXPECT_TRUE(damaged([](Constraint& c) { c.joints.push_back(c.joints.front()); }));
    EXPECT_TRUE(damaged([&](Constraint& c) { c.joints[0].point = s.curve(c.refs[0])->points[0]; }));
}

TEST(Offset, StoresOneJointPerCornerWithItsTangentFlag)
{
    const auto flags = [](const Sketch& s, const Offset& offset) {
        std::vector<bool> tangent;
        for (const OffsetJoint& joint : s.constraint(offset.constraint)->joints) {
            tangent.push_back(joint.tangent);
        }
        return tangent;
    };
    Sketch box;
    const std::optional<Offset> grown = addOffset(box, fixedRectangle(box).sides, 5.0);
    ASSERT_TRUE(grown);
    EXPECT_EQ(flags(box, *grown), std::vector<bool>(4, false));

    Sketch slotted;
    const Slot slot = addSlot(slotted, slotted.addPoint({-10, 0}, true), slotted.addPoint({10, 0}, true), 10.0);
    const std::optional<Offset> around = addOffset(slotted, chainThrough(slotted, slot.sides[0]), 3.0);
    ASSERT_TRUE(around);
    EXPECT_EQ(flags(slotted, *around), std::vector<bool>(4, true));

    Sketch open;
    const Id a = open.addPoint({0, 0}, true);
    const Id b = open.addPoint({30, 0}, true);
    const Id c = open.addPoint({30, 20}, true);
    const std::optional<Offset> bent = addOffset(open, {open.addLine(a, b), open.addLine(b, c)}, 4.0);
    ASSERT_TRUE(bent);
    EXPECT_EQ(flags(open, *bent), std::vector<bool>{false});
    for (const Sketch* s : {&box, &slotted, &open}) {
        EXPECT_FALSE(validate(*s));
    }
}

TEST(Offset, ATangentJointStaysTangentWhenTheSourceBends)
{
    Sketch s;
    const Id middle = s.addPoint({10, 0});
    const std::vector<Id> lines{s.addLine(s.addPoint({0, 0}, true), middle),
                                s.addLine(middle, s.addPoint({20, 0}, true))};
    const std::optional<Offset> offset = addOffset(s, lines, 2.0);
    ASSERT_TRUE(offset);
    ASSERT_EQ(s.constraint(offset->constraint)->joints.size(), 1U);
    EXPECT_TRUE(s.constraint(offset->constraint)->joints[0].tangent);

    ASSERT_TRUE(solve(s, {{middle, {10, 3}}}).solved);
    std::vector<Vec2> bent;
    for (const Id id : offset->curves) {
        for (const Id point : s.curve(id)->points) {
            bent.push_back(s.point(point)->pos);
        }
    }
    ASSERT_TRUE(solve(s).solved);
    std::size_t i = 0;
    for (const Id id : offset->curves) {
        for (const Id point : s.curve(id)->points) {
            EXPECT_NEAR(distance(s.point(point)->pos, bent[i++]), 0.0, kNear);
        }
    }
    EXPECT_TRUE(s.constraint(offset->constraint)->joints[0].tangent);
}

TEST(Offset, ChainsStopAtBranches)
{
    Sketch s;
    const Id hub = s.addPoint({0, 0});
    const Id a = s.addLine(s.addPoint({-10, 0}), hub);
    const Id b = s.addLine(hub, s.addPoint({10, 0}));
    const Id c = s.addLine(hub, s.addPoint({0, 10}));
    EXPECT_EQ(chainThrough(s, a), std::vector<Id>{a});
    EXPECT_FALSE(orderChain(s, {a, b, c}));
    EXPECT_TRUE(orderChain(s, {a, b}));
}

TEST(Offset, SideFollowsTheCursor)
{
    Sketch s;
    const Rectangle rectangle = fixedRectangle(s);
    const std::optional<Chain> chain = orderChain(s, rectangle.sides);
    ASSERT_TRUE(chain);
    EXPECT_TRUE(chain->closed);
    EXPECT_EQ(offsetSide(s, *chain, {20, -3}), 1);
    EXPECT_EQ(offsetSide(s, *chain, {20, 3}), -1);
    EXPECT_EQ(chainThrough(s, rectangle.sides[2]).size(), 4U);
}
