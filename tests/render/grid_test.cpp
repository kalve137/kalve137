// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/grid.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace {

using namespace kalve137::render;
using kalve137::camera::Vec3;

const GridPlane kGround{};

TEST(GridLayout, StepIsOneTwoOrFiveTimesAPowerOfTen)
{
    EXPECT_DOUBLE_EQ(niceGridStep(1.0), 1.0);
    EXPECT_DOUBLE_EQ(niceGridStep(1.2), 2.0);
    EXPECT_DOUBLE_EQ(niceGridStep(3.0), 5.0);
    EXPECT_DOUBLE_EQ(niceGridStep(7.0), 10.0);
    EXPECT_DOUBLE_EQ(niceGridStep(0.013), 0.02);
    EXPECT_DOUBLE_EQ(niceGridStep(450.0), 500.0);
}

TEST(GridLayout, FixedSpacingStaysOnItsLattice)
{
    EXPECT_DOUBLE_EQ(gridStep(false, 2.5, 1.0), 2.5);
    EXPECT_DOUBLE_EQ(gridStep(false, 2.5, 3.0), 12.5);
    EXPECT_DOUBLE_EQ(gridStep(false, 2.5, 20.0), 62.5);
    EXPECT_DOUBLE_EQ(gridStep(true, 2.5, 3.0), 5.0);
    EXPECT_DOUBLE_EQ(gridStep(false, 0.0, 3.0), 5.0);
}

TEST(GridLayout, UnboundedFollowsTheViewOnMajorLines)
{
    const GridLayout layout = layoutGrid(kGround, 1.0, {12.4, -13.0, 7.0}, 300.0, std::nullopt);
    EXPECT_DOUBLE_EQ(layout.step, 1.0);
    EXPECT_DOUBLE_EQ(layout.radius, 300.0);
    EXPECT_DOUBLE_EQ(layout.center.u, 10.0);
    EXPECT_DOUBLE_EQ(layout.center.v, -15.0);
}

TEST(GridLayout, BoundedStaysOnASmallerExtent)
{
    const GridExtent extent{{50.0, 0.0, 0.0}, 120.0};
    const GridLayout layout = layoutGrid(kGround, 2.0, {-400.0, 300.0, 0.0}, 500.0, extent);
    EXPECT_DOUBLE_EQ(layout.radius, 120.0);
    EXPECT_DOUBLE_EQ(layout.center.u, 50.0);
    EXPECT_DOUBLE_EQ(layout.center.v, 0.0);
}

TEST(GridLayout, BoundedFillsTheViewOnceZoomedInPastTheExtent)
{
    const GridExtent extent{{50.0, 0.0, 0.0}, 120.0};
    const GridLayout layout = layoutGrid(kGround, 0.5, {-20.0, 30.0, 0.0}, 80.0, extent);
    EXPECT_DOUBLE_EQ(layout.radius, 80.0);
    EXPECT_DOUBLE_EQ(layout.center.u, -20.0);
    EXPECT_DOUBLE_EQ(layout.center.v, 30.0);
}

TEST(GridLayout, CenterIsInPlaneCoordinates)
{
    const GridPlane front{{0.0, 10.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0}};
    const GridLayout layout = layoutGrid(front, 1.0, {20.0, 99.0, 35.0}, 300.0, std::nullopt);
    EXPECT_DOUBLE_EQ(layout.center.u, 20.0);
    EXPECT_DOUBLE_EQ(layout.center.v, 35.0);
}

TEST(GridLayout, StaleOnlyWhenTheEdgeCouldShow)
{
    const GridLayout current{1.0, {0.0, 0.0}, 100.0};
    EXPECT_FALSE(isGridStale(current, current));
    EXPECT_FALSE(isGridStale(current, {1.0, {5.0, 0.0}, 90.0}));
    EXPECT_TRUE(isGridStale(current, {2.0, {0.0, 0.0}, 100.0}));
    EXPECT_TRUE(isGridStale(current, {1.0, {0.0, 0.0}, 101.0}));
    EXPECT_TRUE(isGridStale(current, {1.0, {0.0, 0.0}, 50.0}));
    EXPECT_TRUE(isGridStale(current, {1.0, {15.0, 0.0}, 100.0}));
}

TEST(GridLayout, LinesFillTheDiskWithAxesAndEveryFifthMajor)
{
    const GridLayout layout{1.0, {0.0, 0.0}, 10.5};
    const std::vector<GridLine> lines = gridLines(layout);
    EXPECT_EQ(lines.size(), 42U);
    const auto count = [&](GridLineKind kind) {
        return std::ranges::count_if(lines, [&](const GridLine& line) { return line.kind == kind; });
    };
    EXPECT_EQ(count(GridLineKind::Axis), 2);
    EXPECT_EQ(count(GridLineKind::Major), 8);
    const auto edge = std::ranges::find_if(lines, [](const GridLine& line) { return line.offset == 10.0; });
    ASSERT_NE(edge, lines.end());
    EXPECT_NEAR(edge->end, std::sqrt((10.5 * 10.5) - 100.0), 1e-12);
    EXPECT_NEAR(edge->start, -edge->end, 1e-12);
}

TEST(GridLayout, FadeStartsAtAThirdOfTheRadius)
{
    const GridLayout layout{1.0, {0.0, 0.0}, 100.0};
    EXPECT_DOUBLE_EQ(gridFade(layout, 30.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(gridFade(layout, 100.0, 0.0), 1.0);
    EXPECT_GT(gridFade(layout, 0.0, 70.0), 0.4);
}

} // namespace
