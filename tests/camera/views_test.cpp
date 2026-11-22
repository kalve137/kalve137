// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "occt_compare.h"

#include "camera/transition.h"
#include "camera/views.h"
#include "viewer/camera_sync.h"
#include "viewer/view_turn.h"

#include <gtest/gtest.h>

#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <gp.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

using namespace kalve137;
using camera::Camera;
using camera::Projection;
using camera::StandardView;
using camera::Vec3;
using camera::ViewTurn;
using test::expectSame;
using test::toDir;

namespace {

constexpr double kTolerance = 1e-9;

Camera placed(Projection projection)
{
    Camera camera;
    camera.setProjectionType(projection);
    camera.setEyeAndCenter({120.0, -340.0, 260.0}, {10.0, 20.0, -5.0});
    camera.setUp({0.0, 0.0, 1.0});
    camera.setScale(300.0);
    camera.setAspect(1.6);
    camera.setZRange(5.0, 2000.0);
    return camera;
}

Handle(Graphic3d_Camera) toOcct(const Camera& camera)
{
    Handle(Graphic3d_Camera) result = new Graphic3d_Camera();
    viewer::applyCamera(camera, *result);
    return result;
}

void occtScale(Graphic3d_Camera& camera, double width, double height)
{
    const double aspect = camera.Aspect();
    camera.SetScale(aspect > 1.0 ? std::max(width / aspect, height) : std::max(width, height * aspect));
}

void occtOrient(Graphic3d_Camera& camera, const gp_Dir& back, const gp_Dir& up)
{
    const gp_Pnt origin = camera.ConvertWorld2View(gp::Origin());
    const double reach = camera.Eye().Distance(gp::Origin());
    camera.SetEyeAndCenter(gp_Pnt(back.XYZ() * reach), gp::Origin());
    camera.SetDirectionFromEye(back.Reversed());
    camera.SetUp(up);
    camera.OrthogonalizeUp();
    const gp_Pnt size = camera.ViewDimensions();
    const gp_Ax3 frame(camera.Center(), camera.Direction().Reversed(), camera.Direction() ^ camera.Up());
    gp_Trsf pan;
    pan.SetTranslation(gp_Vec(frame.XDirection()) * -origin.X() + gp_Vec(frame.YDirection()) * -origin.Y());
    camera.Transform(pan);
    occtScale(camera, size.X(), size.Y());
}

} // namespace

TEST(Views, StandardViewsMatchTheViewCubeOrientations)
{
    const std::vector<std::pair<StandardView, std::pair<gp_Dir, gp_Dir>>> views{
        {StandardView::Front, {gp::DY().Reversed(), gp::DZ()}},
        {StandardView::Back, {gp::DY(), gp::DZ()}},
        {StandardView::Left, {gp::DX().Reversed(), gp::DZ()}},
        {StandardView::Right, {gp::DX(), gp::DZ()}},
        {StandardView::Top, {gp::DZ(), gp::DY()}},
        {StandardView::Bottom, {gp::DZ().Reversed(), gp::DY().Reversed()}},
    };
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        for (const auto& [view, axes] : views) {
            Camera ours = placed(projection);
            const Handle(Graphic3d_Camera) theirs = toOcct(ours);
            camera::standardView(ours, view);
            occtOrient(*theirs, axes.first, axes.second);
            expectSame(ours, *theirs, kTolerance);
        }
        Camera ours = placed(projection);
        const Handle(Graphic3d_Camera) theirs = toOcct(ours);
        camera::homeView(ours);
        occtOrient(*theirs, gp_Dir(1.0, -1.0, 1.0), gp::DZ());
        expectSame(ours, *theirs, kTolerance);
    }
}

TEST(Views, OrientKeepsTheOriginOnScreen)
{
    Camera camera = placed(Projection::Orthographic);
    const Vec3 before = camera.worldToView({});
    camera::standardView(camera, StandardView::Top);
    const Vec3 after = camera.worldToView({});
    EXPECT_NEAR(after.x, before.x, 1e-9);
    EXPECT_NEAR(after.y, before.y, 1e-9);
}

TEST(Views, LooksAtAPlaneLikeTheOldView)
{
    Camera ours = placed(Projection::Orthographic);
    const Handle(Graphic3d_Camera) theirs = toOcct(ours);
    const Vec3 normal = camera::normalized(Vec3{0.0, -1.0, 1.0});
    const Vec3 yDirection = camera::normalized(Vec3{0.0, 1.0, 1.0});
    camera::lookAt(ours, normal, yDirection);
    theirs->SetDirection(toDir(normal).Reversed());
    theirs->SetUp(toDir(yDirection));
    expectSame(ours, *theirs, kTolerance);
}

TEST(Views, TurnsLikeTheViewer)
{
    for (const ViewTurn way : {ViewTurn::Up, ViewTurn::Down, ViewTurn::Left, ViewTurn::Right, ViewTurn::RollClockwise,
                               ViewTurn::RollCounterclockwise}) {
        Camera ours = placed(Projection::Orthographic);
        camera::standardView(ours, StandardView::Front);
        const Handle(Graphic3d_Camera) theirs = toOcct(ours);
        ASSERT_TRUE(camera::facesStandardView(ours));
        ASSERT_TRUE(viewer::facesStandardView(*theirs));
        camera::turn(ours, way);
        viewer::turn(*theirs, way);
        expectSame(ours, *theirs, kTolerance);
    }
    const Camera isometric = [] {
        Camera camera = placed(Projection::Orthographic);
        camera::homeView(camera);
        return camera;
    }();
    EXPECT_FALSE(camera::facesStandardView(isometric));
    EXPECT_FALSE(viewer::facesStandardView(*toOcct(isometric)));
}

TEST(Views, FitAroundAndFitNeedFiniteBounds)
{
    Camera camera = placed(Projection::Orthographic);
    const Camera start = camera;
    EXPECT_FALSE(camera::fit(camera, std::nullopt));
    EXPECT_FALSE(camera::fitAround(camera, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(camera::fitAround(camera, 0.0));
    EXPECT_EQ(camera.eye(), start.eye());
    EXPECT_EQ(camera.scale(), start.scale());
    EXPECT_TRUE(camera::fitAround(camera, 100.0));
    EXPECT_NE(camera.scale(), start.scale());
}

TEST(Transition, MatchesTheOldViewAnimationAlongTheWay)
{
    std::vector<std::pair<Camera, Camera>> moves;
    {
        Camera start = placed(Projection::Orthographic);
        camera::homeView(start);
        Camera end = start;
        camera::standardView(end, StandardView::Front);
        camera::fitAround(end, 50.0);
        moves.emplace_back(start, end);
    }
    {
        Camera start = placed(Projection::Orthographic);
        camera::standardView(start, StandardView::Front);
        Camera end = start;
        camera::standardView(end, StandardView::Back);
        moves.emplace_back(start, end);
    }
    {
        Camera start = placed(Projection::Perspective);
        Camera end = start;
        end.setEyeAndCenter({-80.0, 200.0, 150.0}, {30.0, 0.0, 10.0});
        end.setUp({0.0, 0.3, 1.0});
        end.orthogonalizeUp();
        moves.emplace_back(start, end);
    }
    {
        Camera start = placed(Projection::Orthographic);
        camera::standardView(start, StandardView::Top);
        Camera end = start;
        camera::turn(end, ViewTurn::RollClockwise);
        moves.emplace_back(start, end);
    }
    for (const auto& [start, end] : moves) {
        const camera::Transition transition(start, end);
        const Handle(Graphic3d_Camera) occtStart = toOcct(start);
        const Handle(Graphic3d_Camera) occtEnd = toOcct(end);
        for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            Handle(Graphic3d_Camera) theirs = new Graphic3d_Camera();
            Graphic3d_Camera::Interpolate(occtStart, occtEnd, t, theirs);
            expectSame(transition.at(t * camera::kTransitionSeconds), *theirs, kTolerance);
        }
    }
}

TEST(Transition, EndsOnTheTargetAndCopesWithBadTime)
{
    const Camera start = placed(Projection::Orthographic);
    Camera end = start;
    camera::standardView(end, StandardView::Right);
    const camera::Transition transition(start, end);
    EXPECT_TRUE(transition.isDone(camera::kTransitionSeconds));
    EXPECT_EQ(transition.at(10.0).eye(), end.eye());
    EXPECT_EQ(transition.at(std::numeric_limits<double>::quiet_NaN()).eye(), end.eye());
    EXPECT_EQ(transition.at(-1.0).eye(), start.eye());
    const camera::Transition instant(start, end, 0.0);
    EXPECT_EQ(instant.at(0.0).eye(), end.eye());
}
