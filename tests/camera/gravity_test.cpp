// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "camera/gravity.h"

#include <gtest/gtest.h>

#include <array>

namespace kalve137::camera {
namespace {

constexpr double kTolerance = 1e-9;

Camera lookingDownAt(const Vec3& center, double scale)
{
    Camera camera;
    camera.setEyeAndCenter(center + Vec3{0.0, 0.0, 500.0}, center);
    camera.setUp({0.0, 1.0, 0.0});
    camera.setScale(scale);
    camera.setAspect(1.0);
    camera.setZRange(1.0, 2000.0);
    return camera;
}

TEST(GravityPoint, AveragesTheCornersOnScreen)
{
    const std::array<Box, 2> boxes{Box{{-10.0, -10.0, 0.0}, {10.0, 10.0, 20.0}},
                                   Box{{800.0, 0.0, 0.0}, {810.0, 10.0, 10.0}}};
    const Vec3 point = gravityPoint(lookingDownAt({}, 100.0), boxes);
    EXPECT_NEAR(point.x, 0.0, kTolerance);
    EXPECT_NEAR(point.y, 0.0, kTolerance);
    EXPECT_NEAR(point.z, 10.0, kTolerance);
}

TEST(GravityPoint, FallsBackToTheMiddleOfEverythingWhenNothingIsOnScreen)
{
    const std::array<Box, 2> boxes{Box{{-10.0, -10.0, 0.0}, {10.0, 10.0, 20.0}},
                                   Box{{800.0, 0.0, 0.0}, {810.0, 10.0, 10.0}}};
    const Vec3 point = gravityPoint(lookingDownAt({400.0, 3000.0, 0.0}, 10.0), boxes);
    EXPECT_NEAR(point.x, 400.0, kTolerance);
    EXPECT_NEAR(point.y, 0.0, kTolerance);
    EXPECT_NEAR(point.z, 10.0, kTolerance);
}

TEST(GravityPoint, IsTheOriginForAnEmptyScene)
{
    EXPECT_EQ(gravityPoint(lookingDownAt({}, 100.0), {}), Vec3{});
}

} // namespace
} // namespace kalve137::camera
