// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/grid.h"

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

namespace {

using namespace kalve137::viewer;

TEST(Grid, RoundsToTheIncrement)
{
    EXPECT_DOUBLE_EQ(roundToIncrement(7.3, 2.5), 7.5);
    EXPECT_DOUBLE_EQ(roundToIncrement(-1.2, 1.0), -1.0);
    EXPECT_DOUBLE_EQ(roundToIncrement(44.0, 15.0), 45.0);
    EXPECT_DOUBLE_EQ(roundToIncrement(3.14, 0.0), 3.14);
}

TEST(Grid, PlaneKeepsTheAxesOfTheSketchPlane)
{
    const gp_Ax3 front(gp_Pnt(0.0, 10.0, 0.0), gp::DY().Reversed(), gp::DX());
    const kalve137::render::GridPlane plane = gridPlaneOf(front);
    EXPECT_EQ(plane.origin, (kalve137::camera::Vec3{0.0, 10.0, 0.0}));
    EXPECT_EQ(plane.x, (kalve137::camera::Vec3{1.0, 0.0, 0.0}));
    EXPECT_EQ(plane.y, (kalve137::camera::Vec3{0.0, 0.0, 1.0}));
    EXPECT_EQ(plane.normal, (kalve137::camera::Vec3{0.0, -1.0, 0.0}));
}

} // namespace
