// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/view_turn.h"

#include <gtest/gtest.h>

#include <Graphic3d_Camera.hxx>
#include <gp.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <array>
#include <cmath>
#include <numbers>

using namespace kalve137::viewer;

namespace {

constexpr double kTolerance = 1e-9;
const gp_Pnt kCenter(10.0, 20.0, 30.0);
constexpr double kDistance = 500.0;

Handle(Graphic3d_Camera) looking(const gp_Dir& direction, const gp_Dir& up)
{
    Handle(Graphic3d_Camera) camera = new Graphic3d_Camera();
    camera->SetEyeAndCenter(kCenter.Translated(gp_Vec(direction) * -kDistance), kCenter);
    camera->SetUp(up);
    camera->SetScale(200.0);
    return camera;
}

Handle(Graphic3d_Camera) front()
{
    return looking(gp::DY(), gp::DZ());
}

void expectDir(const gp_Dir& actual, const gp_Dir& expected)
{
    EXPECT_NEAR(actual.X(), expected.X(), kTolerance);
    EXPECT_NEAR(actual.Y(), expected.Y(), kTolerance);
    EXPECT_NEAR(actual.Z(), expected.Z(), kTolerance);
}

} // namespace

TEST(ViewTurn, UpFromFrontShowsTopWithFrontAtTheBottom)
{
    const Handle(Graphic3d_Camera) camera = front();
    turn(*camera, ViewTurn::Up);
    expectDir(camera->Direction(), gp::DZ().Reversed());
    expectDir(camera->Up(), gp::DY());
}

TEST(ViewTurn, SidesAndRollsFromFront)
{
    const Handle(Graphic3d_Camera) left = front();
    turn(*left, ViewTurn::Left);
    expectDir(left->Direction(), gp::DX());
    expectDir(left->Up(), gp::DZ());

    const Handle(Graphic3d_Camera) down = front();
    turn(*down, ViewTurn::Down);
    expectDir(down->Direction(), gp::DZ());
    expectDir(down->Up(), gp::DY().Reversed());

    const Handle(Graphic3d_Camera) roll = front();
    turn(*roll, ViewTurn::RollClockwise);
    expectDir(roll->Direction(), gp::DY());
    expectDir(roll->Direction().Crossed(roll->Up()), gp::DZ());
}

TEST(ViewTurn, FourTurnsComeBack)
{
    for (const ViewTurn way : {ViewTurn::Up, ViewTurn::Down, ViewTurn::Left, ViewTurn::Right, ViewTurn::RollClockwise,
                               ViewTurn::RollCounterclockwise}) {
        const Handle(Graphic3d_Camera) camera = front();
        for (int i = 0; i < 4; ++i) {
            turn(*camera, way);
        }
        expectDir(camera->Direction(), gp::DY());
        expectDir(camera->Up(), gp::DZ());
    }
}

TEST(ViewTurn, KeepsCenterDistanceAndScale)
{
    const Handle(Graphic3d_Camera) camera = front();
    turn(*camera, ViewTurn::Right);
    EXPECT_LT(camera->Center().Distance(kCenter), kTolerance);
    EXPECT_NEAR(camera->Distance(), kDistance, kTolerance);
    EXPECT_DOUBLE_EQ(camera->Scale(), 200.0);
}

TEST(ViewTurn, OnlyFaceViewsCount)
{
    const std::array<gp_Dir, 6> faces{gp::DX(), gp::DX().Reversed(), gp::DY(), gp::DY().Reversed(),
                                      gp::DZ(), gp::DZ().Reversed()};
    for (const gp_Dir& direction : faces) {
        const gp_Dir up = std::abs(direction.Z()) > 0.5 ? gp::DY() : gp::DZ();
        EXPECT_TRUE(facesStandardView(*looking(direction, up)));
    }
    const gp_Dir isometric(-1.0, 1.0, -1.0);
    const gp_Dir isometricUp = gp_Dir(gp_Vec(gp::DZ()) - gp_Vec(isometric) * isometric.Dot(gp::DZ()));
    EXPECT_FALSE(facesStandardView(*looking(isometric, isometricUp)));
}

TEST(ViewTurn, SnapsASlightlyOffAxisView)
{
    const double tilt = 0.2 * std::numbers::pi / 180.0;
    const Handle(Graphic3d_Camera) camera = looking(gp_Dir(std::sin(tilt), std::cos(tilt), 0.0), gp::DZ());
    ASSERT_TRUE(facesStandardView(*camera));
    turn(*camera, ViewTurn::Up);
    expectDir(camera->Direction(), gp::DZ().Reversed());
    expectDir(camera->Up(), gp::DY());
}
