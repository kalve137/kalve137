// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/edit.h"
#include "sketch/sketch.h"
#include "sketch/solver.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace kalve137::sketch;

namespace {

Vec2 at(const Sketch& s, Id point)
{
    return s.point(point)->pos;
}

double lineLength(const Sketch& s, Id line)
{
    const Curve* c = s.curve(line);
    return distance(at(s, c->points[0]), at(s, c->points[1]));
}

} // namespace

TEST(Trim, LineBetweenTwoCrossingsSplitsIt)
{
    Sketch s;
    const Id line = s.addLine(s.addPoint({0, 0}), s.addPoint({30, 0}));
    s.addLine(s.addPoint({10, -5}), s.addPoint({10, 5}));
    s.addLine(s.addPoint({20, -5}), s.addPoint({20, 5}));

    const std::optional<Piece> piece = trimPiece(s, line, {15, 0});
    ASSERT_TRUE(piece);
    EXPECT_FALSE(piece->whole);
    EXPECT_NEAR(piece->start.x, 10.0, 1e-9);
    EXPECT_NEAR(piece->end.x, 20.0, 1e-9);

    std::optional<Sketch> out = trim(s, line, {15, 0});
    ASSERT_TRUE(out);
    EXPECT_EQ(out->curves().size(), 4u);
    EXPECT_NEAR(lineLength(*out, line), 10.0, 1e-9);
    EXPECT_TRUE(solve(*out).solved);
}

TEST(Trim, LineEndPastCrossingShortens)
{
    Sketch s;
    const Id end = s.addPoint({30, 0});
    const Id line = s.addLine(s.addPoint({0, 0}), end);
    const Id cutter = s.addLine(s.addPoint({10, -5}), s.addPoint({10, 5}));
    s.addConstraint(ConstraintKind::Distance, {line}, 30.0);

    std::optional<Sketch> out = trim(s, line, {25, 0});
    ASSERT_TRUE(out);
    EXPECT_NEAR(lineLength(*out, line), 10.0, 1e-9);
    EXPECT_FALSE(out->isPoint(end));
    bool onCutter = false;
    for (const auto& [id, c] : out->constraints()) {
        EXPECT_NE(c.kind, ConstraintKind::Distance);
        onCutter = onCutter || (c.kind == ConstraintKind::PointOnCurve && c.refs[1] == cutter);
    }
    EXPECT_TRUE(onCutter);
}

TEST(Trim, UncrossedCurveIsDeleted)
{
    Sketch s;
    const Id line = s.addLine(s.addPoint({0, 0}), s.addPoint({30, 0}));
    std::optional<Sketch> out = trim(s, line, {15, 0});
    ASSERT_TRUE(out);
    EXPECT_TRUE(out->curves().empty());
}

TEST(Trim, CircleBecomesArcKeepingFarSide)
{
    Sketch s;
    const Id circle = s.addCircle(s.addPoint({0, 0}), 10.0);
    s.addLine(s.addPoint({-20, 0}), s.addPoint({20, 0}));
    s.addConstraint(ConstraintKind::Radius, {circle}, 10.0);

    std::optional<Sketch> out = trim(s, circle, {0, 10});
    ASSERT_TRUE(out);
    EXPECT_FALSE(out->isCurve(circle));
    const Curve* arc = nullptr;
    Id arcId = 0;
    for (const auto& [id, c] : out->curves()) {
        if (c.kind == CurveKind::Arc) {
            arc = &c;
            arcId = id;
        }
    }
    ASSERT_NE(arc, nullptr);
    EXPECT_NEAR(at(*out, arc->points[1]).x, -10.0, 1e-9);
    EXPECT_NEAR(at(*out, arc->points[2]).x, 10.0, 1e-9);
    bool radiusMoved = false;
    for (const auto& [id, c] : out->constraints()) {
        radiusMoved = radiusMoved || (c.kind == ConstraintKind::Radius && c.refs[0] == arcId);
    }
    EXPECT_TRUE(radiusMoved);
    EXPECT_TRUE(solve(*out).solved);
}

TEST(Trim, RectangleCornerReusesExistingPoint)
{
    Sketch s;
    const Id a = s.addPoint({0, 0});
    const Id b = s.addPoint({10, 0});
    const Id c = s.addPoint({10, 10});
    s.addLine(a, b);
    const Id right = s.addLine(b, c);
    const Id overhang = s.addLine(b, s.addPoint({15, 0}));
    const std::size_t points = s.points().size();
    std::optional<Sketch> out = trim(s, overhang, {13, 0});
    ASSERT_TRUE(out);
    EXPECT_FALSE(out->isCurve(overhang));
    EXPECT_TRUE(out->isCurve(right));
    EXPECT_EQ(out->points().size(), points - 1);
}

TEST(FilletCorner, RoundsRightAngle)
{
    Sketch s;
    const Id corner = s.addPoint({10, 0});
    const Id bottom = s.addLine(s.addPoint({0, 0}), corner);
    const Id side = s.addLine(corner, s.addPoint({10, 10}));
    s.addConstraint(ConstraintKind::Horizontal, {bottom});
    s.addConstraint(ConstraintKind::Vertical, {side});

    std::optional<Sketch> out = filletCorner(s, corner, 2.0);
    ASSERT_TRUE(out);
    EXPECT_FALSE(out->isPoint(corner));
    EXPECT_NEAR(lineLength(*out, bottom), 8.0, 1e-9);
    EXPECT_NEAR(lineLength(*out, side), 8.0, 1e-9);
    const SolveResult result = solve(*out);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.conflicting.empty());
    for (const auto& [id, c] : out->curves()) {
        if (c.kind == CurveKind::Arc) {
            EXPECT_NEAR(at(*out, c.points[0]).x, 8.0, 1e-6);
            EXPECT_NEAR(at(*out, c.points[0]).y, 2.0, 1e-6);
        }
    }
}

TEST(FilletCorner, RectangleCornerAddsNoRedundancy)
{
    Sketch s;
    const Id p0 = kOriginId;
    const Id p1 = s.addPoint({60, 0});
    const Id p2 = s.addPoint({60, 40});
    const Id p3 = s.addPoint({0, 40});
    const Id bottom = s.addLine(p0, p1);
    const Id right = s.addLine(p1, p2);
    const Id top = s.addLine(p2, p3);
    const Id left = s.addLine(p3, p0);
    s.addConstraint(ConstraintKind::Horizontal, {bottom});
    s.addConstraint(ConstraintKind::Horizontal, {top});
    s.addConstraint(ConstraintKind::Vertical, {right});
    s.addConstraint(ConstraintKind::Vertical, {left});
    Sketch before = s;
    const std::size_t redundant = solve(before).redundant.size();

    std::optional<Sketch> out = filletCorner(s, p3, 8.0);
    ASSERT_TRUE(out);
    const SolveResult result = solve(*out);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.conflicting.empty());
    EXPECT_EQ(result.redundant.size(), redundant);
}

TEST(FilletCorner, DimensionedRectangleStaysFullyConstrained)
{
    Sketch s;
    const Id p1 = s.addPoint({60, 0});
    const Id p2 = s.addPoint({60, 40});
    const Id p3 = s.addPoint({0, 40});
    const Id bottom = s.addLine(kOriginId, p1);
    const Id right = s.addLine(p1, p2);
    const Id top = s.addLine(p2, p3);
    const Id left = s.addLine(p3, kOriginId);
    s.addConstraint(ConstraintKind::Horizontal, {bottom});
    s.addConstraint(ConstraintKind::Horizontal, {top});
    s.addConstraint(ConstraintKind::Vertical, {right});
    s.addConstraint(ConstraintKind::Vertical, {left});
    s.addConstraint(ConstraintKind::Distance, {bottom}, 60.0);
    s.addConstraint(ConstraintKind::Distance, {right}, 40.0);

    for (Id corner : {p2, kOriginId}) {
        std::optional<Sketch> out = filletCorner(s, corner, 8.0);
        ASSERT_TRUE(out);
        EXPECT_TRUE(out->isPoint(corner));
        const SolveResult result = solve(*out);
        EXPECT_TRUE(result.solved);
        EXPECT_TRUE(result.redundant.empty());
        EXPECT_EQ(result.dof, 0);
        EXPECT_NEAR(out->point(corner)->pos.x, corner == p2 ? 60.0 : 0.0, 1e-6);
    }
}

TEST(Trim, EndingOnExistingPointAddsNoRedundancy)
{
    Sketch s;
    const Id top = s.addLine(s.addPoint({0, 40}), s.addPoint({60, 40}));
    const Id foot = s.addPoint({32, 40});
    s.addLine(s.addPoint({32, 0}), foot);
    s.addConstraint(ConstraintKind::PointOnCurve, {foot, top});
    Sketch before = s;
    const std::size_t redundant = solve(before).redundant.size();

    std::optional<Sketch> out = trim(s, top, {50, 40});
    ASSERT_TRUE(out);
    EXPECT_EQ(out->curve(top)->points[1], foot);
    const SolveResult result = solve(*out);
    EXPECT_TRUE(result.solved);
    EXPECT_TRUE(result.conflicting.empty());
    EXPECT_EQ(result.redundant.size(), redundant);
}

TEST(FilletCorner, RefusesTooLargeRadius)
{
    Sketch s;
    const Id corner = s.addPoint({10, 0});
    s.addLine(s.addPoint({0, 0}), corner);
    s.addLine(corner, s.addPoint({10, 3}));
    EXPECT_FALSE(filletCorner(s, corner, 5.0));
}
