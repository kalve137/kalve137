// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/grid.h"

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <optional>

namespace {

using namespace kalve137::viewer;

const gp_Ax3 kGround(gp::Origin(), gp::DZ(), gp::DX());

TEST(Grid, StepIsOneTwoOrFiveTimesAPowerOfTen)
{
    EXPECT_DOUBLE_EQ(niceGridStep(1.0), 1.0);
    EXPECT_DOUBLE_EQ(niceGridStep(1.2), 2.0);
    EXPECT_DOUBLE_EQ(niceGridStep(3.0), 5.0);
    EXPECT_DOUBLE_EQ(niceGridStep(7.0), 10.0);
    EXPECT_DOUBLE_EQ(niceGridStep(0.013), 0.02);
    EXPECT_DOUBLE_EQ(niceGridStep(450.0), 500.0);
}

TEST(Grid, FixedSpacingStaysOnItsLattice)
{
    EXPECT_DOUBLE_EQ(gridStep(false, 2.5, 1.0), 2.5);
    EXPECT_DOUBLE_EQ(gridStep(false, 2.5, 3.0), 12.5);
    EXPECT_DOUBLE_EQ(gridStep(false, 2.5, 20.0), 62.5);
    EXPECT_DOUBLE_EQ(gridStep(true, 2.5, 3.0), 5.0);
    EXPECT_DOUBLE_EQ(gridStep(false, 0.0, 3.0), 5.0);
}

TEST(Grid, RoundsToTheIncrement)
{
    EXPECT_DOUBLE_EQ(roundToIncrement(7.3, 2.5), 7.5);
    EXPECT_DOUBLE_EQ(roundToIncrement(-1.2, 1.0), -1.0);
    EXPECT_DOUBLE_EQ(roundToIncrement(44.0, 15.0), 45.0);
    EXPECT_DOUBLE_EQ(roundToIncrement(3.14, 0.0), 3.14);
}

TEST(Grid, UnboundedFollowsTheViewOnMajorLines)
{
    const GridLayout layout = layoutGrid(kGround, 1.0, gp_Pnt(12.4, -13.0, 7.0), 300.0, std::nullopt);
    EXPECT_DOUBLE_EQ(layout.step, 1.0);
    EXPECT_DOUBLE_EQ(layout.radius, 300.0);
    EXPECT_DOUBLE_EQ(layout.center.X(), 10.0);
    EXPECT_DOUBLE_EQ(layout.center.Y(), -15.0);
}

TEST(Grid, BoundedStaysOnASmallerExtent)
{
    const GridExtent extent{gp_Pnt(50.0, 0.0, 0.0), 120.0};
    const GridLayout layout = layoutGrid(kGround, 2.0, gp_Pnt(-400.0, 300.0, 0.0), 500.0, extent);
    EXPECT_DOUBLE_EQ(layout.radius, 120.0);
    EXPECT_DOUBLE_EQ(layout.center.X(), 50.0);
    EXPECT_DOUBLE_EQ(layout.center.Y(), 0.0);
}

TEST(Grid, BoundedFillsTheViewOnceZoomedInPastTheExtent)
{
    const GridExtent extent{gp_Pnt(50.0, 0.0, 0.0), 120.0};
    const GridLayout layout = layoutGrid(kGround, 0.5, gp_Pnt(-20.0, 30.0, 0.0), 80.0, extent);
    EXPECT_DOUBLE_EQ(layout.radius, 80.0);
    EXPECT_DOUBLE_EQ(layout.center.X(), -20.0);
    EXPECT_DOUBLE_EQ(layout.center.Y(), 30.0);
}

TEST(Grid, CenterIsInPlaneCoordinates)
{
    const gp_Ax3 front(gp_Pnt(0.0, 10.0, 0.0), gp::DY().Reversed(), gp::DX());
    const GridLayout layout = layoutGrid(front, 1.0, gp_Pnt(20.0, 99.0, 35.0), 300.0, std::nullopt);
    EXPECT_DOUBLE_EQ(layout.center.X(), 20.0);
    EXPECT_DOUBLE_EQ(layout.center.Y(), 35.0);
}

TEST(Grid, StaleOnlyWhenTheEdgeCouldShow)
{
    const GridLayout current{1.0, gp_Pnt2d(0.0, 0.0), 100.0};
    EXPECT_FALSE(isGridStale(current, current));
    EXPECT_FALSE(isGridStale(current, {1.0, gp_Pnt2d(5.0, 0.0), 90.0}));
    EXPECT_TRUE(isGridStale(current, {2.0, gp_Pnt2d(0.0, 0.0), 100.0}));
    EXPECT_TRUE(isGridStale(current, {1.0, gp_Pnt2d(0.0, 0.0), 101.0}));
    EXPECT_TRUE(isGridStale(current, {1.0, gp_Pnt2d(0.0, 0.0), 50.0}));
    EXPECT_TRUE(isGridStale(current, {1.0, gp_Pnt2d(15.0, 0.0), 100.0}));
}

} // namespace
