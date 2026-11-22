// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../camera/occt_compare.h"

#include "camera/camera.h"
#include "camera/gravity.h"
#include "viewer/occt_view_widget.h"

#include <gtest/gtest.h>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Graphic3d_Camera.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax3.hxx>

#include <QTest>

#include <array>
#include <optional>
#include <utility>

using namespace kalve137;
using camera::Camera;
using camera::Projection;
using camera::Vec3;

namespace {

constexpr double kTolerance = 1e-9;
constexpr camera::Viewport kViewport{1600, 1000, 1.25};

Camera placed(Projection projection)
{
    Camera camera;
    camera.setProjectionType(projection);
    camera.setEyeAndCenter({120.0, -340.0, 260.0}, {10.0, 20.0, -5.0});
    camera.setUp({0.0, 0.0, 1.0});
    camera.setScale(300.0);
    camera.setAspect(kViewport.aspect());
    camera.setZRange(5.0, 2000.0);
    return camera;
}

Handle(Graphic3d_Camera) occtCamera(const viewer::OcctViewWidget& view)
{
    return view.aisContext()->CurrentViewer()->DefinedViews().First()->Camera();
}

} // namespace

TEST(SyncedView, HasNoWindowAndProjectsNothingBeforeTheFirstSync)
{
    viewer::OcctViewWidget hidden;
    ASSERT_FALSE(hidden.isReady());
    EXPECT_FALSE(hidden.viewport());
    EXPECT_FALSE(hidden.pickOnPlane({10.0, 10.0}, gp_Ax3()));
}

TEST(SyncedView, MatricesFollowTheCameraInPerspectiveAndOrthographic)
{
    viewer::OcctViewWidget hidden;
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        const Camera camera = placed(projection);
        hidden.setSyncedCamera(camera, kViewport);
        const Handle(Graphic3d_Camera) occt = occtCamera(hidden);
        test::expectMatrix(camera.view(), occt->OrientationMatrix(), kTolerance);
        test::expectMatrix(camera.projection(), occt->ProjectionMatrix(), kTolerance);
        test::expectSame(camera, *occt, kTolerance);
    }
}

TEST(SyncedView, ProjectsAndPicksLikeTheCameraWithoutAWindow)
{
    viewer::OcctViewWidget hidden;
    const gp_Ax3 plane(gp_Pnt(0.0, 0.0, 12.0), gp_Dir(0.0, 0.0, 1.0));
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        const Camera camera = placed(projection);
        hidden.setSyncedCamera(camera, kViewport);
        ASSERT_TRUE(hidden.viewport());

        for (const Vec3 point : {Vec3{10.0, 20.0, -5.0}, Vec3{-40.0, 15.0, 30.0}, Vec3{60.0, -25.0, 0.0}}) {
            const QPointF screen = hidden.toScreen(test::toPnt(point));
            const camera::ScreenPoint expected = camera.project(point, kViewport);
            EXPECT_NEAR(screen.x(), expected.x, 1e-6);
            EXPECT_NEAR(screen.y(), expected.y, 1e-6);

            const gp_Dir toward = hidden.towardViewer(test::toPnt(point));
            const Vec3 back = camera.isOrthographic() ? -camera.direction() : normalized(camera.eye() - point);
            test::expectNear(back, toward.XYZ(), kTolerance);
        }

        for (const QPointF pos : {QPointF(640.0, 400.0), QPointF(100.5, 700.25), QPointF(1200.0, 80.0)}) {
            const std::optional<gp_Pnt> hit = hidden.pickOnPlane(pos, plane);
            ASSERT_TRUE(hit);
            const camera::Ray ray = camera.unproject(camera::ScreenPoint{pos.x(), pos.y()}, kViewport);
            const double t = (12.0 - ray.origin.z) / ray.direction.z;
            test::expectNear(ray.origin + ray.direction * t, hit->XYZ(), 1e-7 * camera.distance());
        }
        EXPECT_NEAR(hidden.worldPerPixel(), camera.worldPerPixel(kViewport), kTolerance);
    }
}

TEST(SyncedView, FindsTheBoundsAndGravityPointOfAShownView)
{
    viewer::OcctViewWidget shown;
    shown.resize(640, 400);
    shown.show();
    ASSERT_TRUE(QTest::qWaitFor([&] { return shown.isReady(); }, 5000));
    viewer::OcctViewWidget hidden;
    for (viewer::OcctViewWidget* view : {&shown, &hidden}) {
        for (const auto& [corner, size] :
             {std::pair{gp_Pnt(-20.0, -10.0, 0.0), 40.0}, {gp_Pnt(900.0, 0.0, 0.0), 10.0}}) {
            const Handle(AIS_Shape) box = new AIS_Shape(BRepPrimAPI_MakeBox(corner, size, size, size).Shape());
            view->aisContext()->Display(box, AIS_Shaded, 0, false);
        }
    }
    const camera::Viewport viewport = *shown.viewport();
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        Camera camera = placed(projection);
        camera.setAspect(viewport.aspect());
        shown.setSyncedCamera(camera, viewport);
        hidden.setSyncedCamera(camera, viewport);

        const std::optional<camera::Box> expected = shown.sceneBounds();
        const std::optional<camera::Box> bounds = hidden.sceneBounds();
        ASSERT_TRUE(expected);
        ASSERT_TRUE(bounds);
        test::expectNear(bounds->min, test::toPnt(expected->min).XYZ(), kTolerance);
        test::expectNear(bounds->max, test::toPnt(expected->max).XYZ(), kTolerance);
        const gp_Pnt gravity = hidden.gravityPoint();
        const gp_Pnt expectedGravity = shown.gravityPoint();
        EXPECT_LT(gravity.Distance(expectedGravity), kTolerance) << "gravity point";
        EXPECT_LT(expectedGravity.X(), 100.0) << "the far box is off screen";
        const std::array<camera::Box, 2> boxes{camera::Box{{-20.0, -10.0, 0.0}, {20.0, 30.0, 40.0}},
                                               camera::Box{{900.0, 0.0, 0.0}, {910.0, 10.0, 10.0}}};
        test::expectNear(camera::gravityPoint(camera, boxes), expectedGravity.XYZ(), 1e-6);
    }
}

TEST(SyncedView, IgnoresAnEmptyViewport)
{
    viewer::OcctViewWidget hidden;
    hidden.setSyncedCamera(placed(Projection::Orthographic), camera::Viewport{0, 0, 1.0});
    EXPECT_FALSE(hidden.viewport());
}
