// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sketch/draft.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace kalve137::sketch;

namespace {

constexpr double kTolerance = 1e-9;

}

TEST(SketchDraft, FollowsCursorWhenNothingHeld)
{
    const Vec2 end = lineEnd({1.0, 1.0}, {4.0, 5.0}, std::nullopt, std::nullopt, std::nullopt);
    EXPECT_NEAR(end.x, 4.0, kTolerance);
    EXPECT_NEAR(end.y, 5.0, kTolerance);
}

TEST(SketchDraft, HeldLengthKeepsCursorDirection)
{
    const Vec2 end = lineEnd({0.0, 0.0}, {3.0, 4.0}, std::nullopt, 10.0, std::nullopt);
    EXPECT_NEAR(end.x, 6.0, kTolerance);
    EXPECT_NEAR(end.y, 8.0, kTolerance);
}

TEST(SketchDraft, HeldAngleTurnsFromPreviousSegment)
{
    const Vec2 up = lineEnd({0.0, 0.0}, {2.0, 5.0}, Vec2{1.0, 0.0}, std::nullopt, 90.0);
    EXPECT_NEAR(up.x, 0.0, kTolerance);
    EXPECT_NEAR(up.y, 5.0, kTolerance);
    EXPECT_NEAR(interiorAngle({1.0, 0.0}, up), 90.0, 1e-6);
}

TEST(SketchDraft, CursorPicksAngleSide)
{
    const Vec2 down = lineEnd({0.0, 0.0}, {2.0, -5.0}, Vec2{1.0, 0.0}, std::nullopt, 90.0);
    EXPECT_NEAR(down.x, 0.0, kTolerance);
    EXPECT_NEAR(down.y, -5.0, kTolerance);
}

TEST(SketchDraft, HeldLengthAndAngle)
{
    const Vec2 end = lineEnd({0.0, 0.0}, {-1.0, 0.1}, Vec2{1.0, 0.0}, 2.0, 45.0);
    EXPECT_NEAR(end.x, -std::sqrt(2.0), kTolerance);
    EXPECT_NEAR(end.y, std::sqrt(2.0), kTolerance);
    EXPECT_NEAR(interiorAngle({1.0, 0.0}, end), 45.0, 1e-6);
}

TEST(SketchDraft, InteriorAngleRange)
{
    EXPECT_NEAR(interiorAngle({1.0, 0.0}, {1.0, 0.0}), 180.0, 1e-6);
    EXPECT_NEAR(interiorAngle({1.0, 0.0}, {-1.0, 0.0}), 0.0, 1e-6);
}

TEST(SketchDraft, SnapToGridRoundsToNearestIntersection)
{
    const Vec2 snapped = snapToGrid({44.2, -35.6}, 5.0);
    EXPECT_NEAR(snapped.x, 45.0, 1e-9);
    EXPECT_NEAR(snapped.y, -35.0, 1e-9);
    const Vec2 fine = snapToGrid({0.26, 0.74}, 0.5);
    EXPECT_NEAR(fine.x, 0.5, 1e-9);
    EXPECT_NEAR(fine.y, 0.5, 1e-9);
}

TEST(SketchDraft, SnapToGridWithoutStepKeepsPoint)
{
    const Vec2 kept = snapToGrid({1.23, 4.56}, 0.0);
    EXPECT_DOUBLE_EQ(kept.x, 1.23);
    EXPECT_DOUBLE_EQ(kept.y, 4.56);
}
