// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/camera_guard.h"

#include <gtest/gtest.h>

#include <Graphic3d_Camera.hxx>
#include <gp.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <limits>

using namespace kalve137::viewer;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

Handle(Graphic3d_Camera) lookingDown()
{
    Handle(Graphic3d_Camera) camera = new Graphic3d_Camera();
    camera->SetEyeAndCenter(gp_Pnt(0, 0, 100), gp::Origin());
    camera->SetUp(gp::DY());
    camera->SetScale(200.0);
    return camera;
}

} // namespace

TEST(CameraGuard, AcceptsAnOrdinaryCamera)
{
    EXPECT_TRUE(isUsable(*lookingDown()));
}

TEST(CameraGuard, RejectsNaNPlacementAndBadScale)
{
    const Handle(Graphic3d_Camera) nanCenter = lookingDown();
    nanCenter->SetCenter(gp_Pnt(kNaN, kNaN, kNaN));
    EXPECT_FALSE(isUsable(*nanCenter));

    const Handle(Graphic3d_Camera) nanScale = lookingDown();
    nanScale->SetScale(kNaN);
    EXPECT_FALSE(isUsable(*nanScale));
}

TEST(CameraGuard, ResetRebuildsANaNCamera)
{
    const Handle(Graphic3d_Camera) camera = lookingDown();
    camera->SetCenter(gp_Pnt(kNaN, kNaN, kNaN));
    ASSERT_FALSE(isUsable(*camera));
    resetCamera(*camera, 100.0);
    EXPECT_TRUE(isUsable(*camera));
    EXPECT_LT(camera->Center().Distance(gp::Origin()), 1e-9);
}

TEST(CameraGuard, RepairsUpParallelToTheViewDirection)
{
    const Handle(Graphic3d_Camera) camera = lookingDown();
    EXPECT_FALSE(repairUp(*camera));
    camera->SetUp(gp::DZ());
    ASSERT_TRUE(repairUp(*camera));
    EXPECT_LT(std::abs(camera->Direction().Dot(camera->Up())), 1e-6);
    EXPECT_TRUE(isUsable(*camera));
}
