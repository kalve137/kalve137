// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "occt_compare.h"

#include "camera/camera.h"
#include "viewer/camera_guard.h"
#include "viewer/camera_sync.h"

#include <gtest/gtest.h>

#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

using namespace kalve137;
using camera::Camera;
using camera::Projection;
using camera::Vec3;
using test::expectMatrix;
using test::expectSame;
using test::toDir;
using test::toPnt;

namespace {

constexpr double kTolerance = 1e-9;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Pair {
    Camera ours;
    Handle(Graphic3d_Camera) theirs = new Graphic3d_Camera();

    void eyeAndCenter(const Vec3& eye, const Vec3& center)
    {
        ours.setEyeAndCenter(eye, center);
        theirs->SetEyeAndCenter(toPnt(eye), toPnt(center));
    }
    void up(const Vec3& up)
    {
        ours.setUp(up);
        theirs->SetUp(toDir(up));
    }
    void scale(double scale)
    {
        ours.setScale(scale);
        theirs->SetScale(scale);
    }
    void aspect(double aspect)
    {
        ours.setAspect(aspect);
        theirs->SetAspect(aspect);
    }
    void zRange(double zNear, double zFar)
    {
        ours.setZRange(zNear, zFar);
        theirs->SetZRange(zNear, zFar);
    }
    void projection(Projection projection)
    {
        ours.setProjectionType(projection);
        theirs->SetProjectionType(projection == Projection::Perspective ? Graphic3d_Camera::Projection_Perspective
                                                                        : Graphic3d_Camera::Projection_Orthographic);
    }
};

Pair placed(Projection projection, double aspect)
{
    Pair pair;
    pair.projection(projection);
    pair.eyeAndCenter({120.0, -340.0, 260.0}, {10.0, 20.0, -5.0});
    pair.up({0.0, 0.0, 1.0});
    pair.scale(300.0);
    pair.aspect(aspect);
    pair.zRange(5.0, 2000.0);
    return pair;
}

Bnd_Box toBox(const camera::Box& box)
{
    Bnd_Box result;
    result.Update(box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z);
    return result;
}

void occtFit(Graphic3d_Camera& camera, const camera::Box& box, double margin)
{
    ASSERT_TRUE(camera.FitMinMax(toBox(box), 1e-6, true));
    const gp_Pnt size = camera.ViewDimensions();
    const double width = size.X() * (1.0 + margin);
    const double height = size.Y() * (1.0 + margin);
    const double aspect = camera.Aspect();
    camera.SetScale(aspect > 1.0 ? std::max(width / aspect, height) : std::max(width, height * aspect));
}

} // namespace

TEST(Camera, StartsLikeTheOldViewCamera)
{
    const Camera ours;
    const Handle(Graphic3d_Camera) theirs = new Graphic3d_Camera();
    expectSame(ours, *theirs, kTolerance);
    expectMatrix(ours.view(), theirs->OrientationMatrix(), kTolerance);
    expectMatrix(ours.projection(), theirs->ProjectionMatrix(), kTolerance);
}

TEST(Camera, MatricesMatchInPerspectiveAndOrthographic)
{
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        for (const double aspect : {1.6, 0.7}) {
            const Pair pair = placed(projection, aspect);
            expectSame(pair.ours, *pair.theirs, kTolerance);
            expectMatrix(pair.ours.view(), pair.theirs->OrientationMatrix(), kTolerance);
            expectMatrix(pair.ours.projection(), pair.theirs->ProjectionMatrix(), kTolerance);
        }
    }
}

TEST(Camera, SettersMoveTheSameWay)
{
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        Pair pair = placed(projection, 1.6);
        const std::vector<std::function<void(Pair&)>> steps{
            [](Pair& p) {
                p.ours.setCenter({40.0, -10.0, 15.0});
                p.theirs->SetCenter(gp_Pnt(40.0, -10.0, 15.0));
            },
            [](Pair& p) {
                p.ours.setEye({-200.0, -150.0, 90.0});
                p.theirs->SetEye(gp_Pnt(-200.0, -150.0, 90.0));
            },
            [](Pair& p) {
                p.ours.setDirection({0.3, 0.9, -0.2});
                p.theirs->SetDirection(gp_Dir(0.3, 0.9, -0.2));
            },
            [](Pair& p) {
                p.ours.setDirectionFromEye({-0.5, 0.5, -0.7});
                p.theirs->SetDirectionFromEye(gp_Dir(-0.5, 0.5, -0.7));
            },
            [](Pair& p) {
                p.ours.setDistance(750.0);
                p.theirs->SetDistance(750.0);
            },
            [](Pair& p) { p.scale(123.0); },
            [](Pair& p) {
                p.ours.orthogonalizeUp();
                p.theirs->OrthogonalizeUp();
            },
            [](Pair& p) {
                gp_Trsf turn;
                turn.SetRotation(gp_Ax1(gp_Pnt(5.0, 6.0, 7.0), gp_Dir(0.2, -0.4, 0.9)), 0.7);
                p.theirs->Transform(turn);
                p.ours.transform(camera::Motion::rotationAbout({5.0, 6.0, 7.0}, {0.2, -0.4, 0.9}, 0.7));
            },
            [](Pair& p) {
                gp_Trsf shift;
                shift.SetTranslation(gp_Vec(30.0, -20.0, 10.0));
                p.theirs->Transform(shift);
                p.ours.transform(camera::Motion::translationBy({30.0, -20.0, 10.0}));
            },
            [projection](Pair& p) {
                p.projection(projection == Projection::Perspective ? Projection::Orthographic
                                                                   : Projection::Perspective);
            },
            [](Pair& p) { p.scale(456.0); },
        };
        for (const auto& step : steps) {
            step(pair);
            expectSame(pair.ours, *pair.theirs, kTolerance);
            expectMatrix(pair.ours.view(), pair.theirs->OrientationMatrix(), kTolerance);
            expectMatrix(pair.ours.projection(), pair.theirs->ProjectionMatrix(), kTolerance);
        }
    }
}

TEST(Camera, ProjectsAndUnprojectsTheSame)
{
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        const Pair pair = placed(projection, 1.6);
        const Vec3 point{35.0, -12.0, 40.0};
        test::expectNear(pair.ours.project(point), pair.theirs->Project(toPnt(point)).XYZ(), kTolerance);
        const Vec3 normalized{0.25, -0.4, 0.3};
        test::expectNear(pair.ours.unproject(normalized), pair.theirs->UnProject(toPnt(normalized)).XYZ(),
                         kTolerance * 1000.0);
        test::expectNear(pair.ours.worldToView(point), pair.theirs->ConvertWorld2View(toPnt(point)).XYZ(),
                         kTolerance * 1000.0);
        const gp_Pnt size = pair.theirs->ViewDimensions(77.0);
        test::expectNear(pair.ours.viewDimensions(77.0), size.XYZ(), kTolerance * 1000.0);
    }
}

TEST(Camera, PixelsRoundTrip)
{
    const Pair pair = placed(Projection::Perspective, 1.6);
    const camera::Viewport viewport{1600, 1000, 1.25};
    const Vec3 point{35.0, -12.0, 40.0};
    const camera::ScreenPoint screen = pair.ours.project(point, viewport);
    const camera::Ray ray = pair.ours.unproject(screen, viewport);
    const Vec3 toPoint = point - ray.origin;
    const double off = camera::length(toPoint - ray.direction * camera::dot(toPoint, ray.direction));
    EXPECT_LT(off, pair.ours.worldPerPixel(viewport) * 2.0);
}

TEST(Camera, FitsLikeFitMinMax)
{
    const camera::Box box{{-40.0, -10.0, 0.0}, {60.0, 25.0, 30.0}};
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        for (const double aspect : {1.6, 0.7}) {
            Pair pair = placed(projection, aspect);
            ASSERT_TRUE(pair.ours.fit(box, 0.3));
            occtFit(*pair.theirs, box, 0.3);
            expectSame(pair.ours, *pair.theirs, kTolerance);
        }
    }
}

TEST(Camera, FitsDepthLikeZFitAll)
{
    const camera::Box box{{-40.0, -10.0, 0.0}, {60.0, 25.0, 30.0}};
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        Pair pair = placed(projection, 1.6);
        double zNear = 0.0;
        double zFar = 0.0;
        pair.theirs->ZFitAll(1.0, toBox(box), toBox(box), zNear, zFar);
        pair.ours.fitDepth(box);
        EXPECT_NEAR(pair.ours.zNear(), zNear, kTolerance * std::abs(zNear) + kTolerance);
        EXPECT_NEAR(pair.ours.zFar(), zFar, kTolerance * std::abs(zFar));
    }
}

TEST(Camera, EmptyDepthFitFallsBackToDefaults)
{
    Camera camera;
    camera.setZRange(10.0, 20.0);
    camera.fitDepth(std::nullopt);
    EXPECT_DOUBLE_EQ(camera.zNear(), Graphic3d_Camera().ZNear());
    EXPECT_DOUBLE_EQ(camera.zFar(), Graphic3d_Camera().ZFar());
}

TEST(Camera, RejectsNumbersThatAreNotFinite)
{
    const Camera start = placed(Projection::Perspective, 1.6).ours;
    Camera camera = start;
    EXPECT_FALSE(camera.setEyeAndCenter({kNan, 0.0, 0.0}, {}));
    EXPECT_FALSE(camera.setEyeAndCenter({}, {0.0, kInf, 0.0}));
    EXPECT_FALSE(camera.setEye({kInf, 0.0, 0.0}));
    EXPECT_FALSE(camera.setCenter({0.0, 0.0, kNan}));
    EXPECT_FALSE(camera.setUp({kNan, 0.0, 1.0}));
    EXPECT_FALSE(camera.setUp({}));
    EXPECT_FALSE(camera.setDirection({0.0, kInf, 0.0}));
    EXPECT_FALSE(camera.setDirectionFromEye({}));
    EXPECT_FALSE(camera.setDistance(kNan));
    EXPECT_FALSE(camera.setScale(kNan));
    EXPECT_FALSE(camera.setScale(0.0));
    EXPECT_FALSE(camera.setScale(-1.0));
    EXPECT_FALSE(camera.setAspect(kInf));
    EXPECT_FALSE(camera.setAspect(0.0));
    EXPECT_FALSE(camera.setFovY(kNan));
    EXPECT_FALSE(camera.setZRange(kNan, 10.0));
    EXPECT_FALSE(camera.setZRange(10.0, 5.0));
    EXPECT_FALSE(camera.setZRange(0.0, 5.0));
    EXPECT_FALSE(camera.fit({{kNan, 0.0, 0.0}, {1.0, 1.0, 1.0}}, 0.3));
    EXPECT_FALSE(camera.fit({{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}, kNan));
    camera.fitDepth(camera::Box{{kNan, 0.0, 0.0}, {1.0, 1.0, 1.0}});
    EXPECT_TRUE(camera.isUsable());
    EXPECT_EQ(camera.eye(), start.eye());
    EXPECT_EQ(camera.center(), start.center());
    EXPECT_EQ(camera.up(), start.up());
    EXPECT_EQ(camera.scale(), start.scale());
}

TEST(Camera, RepairsAnUpAlongTheViewLikeTheGuard)
{
    for (const Vec3 direction : {Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, -1.0}}) {
        Pair pair = placed(Projection::Orthographic, 1.6);
        pair.ours.setDirection(direction);
        pair.theirs->SetDirection(toDir(direction));
        pair.up(direction);
        EXPECT_TRUE(pair.ours.repairUp());
        viewer::repairUp(*pair.theirs);
        expectSame(pair.ours, *pair.theirs, kTolerance);
    }
}

TEST(CameraSync, ReadsBackWhatItApplies)
{
    for (const Projection projection : {Projection::Perspective, Projection::Orthographic}) {
        Pair pair = placed(projection, 0.7);
        pair.ours.setFovY(30.0);
        const Handle(Graphic3d_Camera) target = new Graphic3d_Camera();
        viewer::applyCamera(pair.ours, *target);
        expectSame(pair.ours, *target, kTolerance);
        expectMatrix(pair.ours.view(), target->OrientationMatrix(), kTolerance);
        expectMatrix(pair.ours.projection(), target->ProjectionMatrix(), kTolerance);
        const Camera back = viewer::readCamera(target);
        expectSame(back, *target, kTolerance);
        const Handle(Graphic3d_Camera) copy = new Graphic3d_Camera();
        copy->Copy(target);
        copy->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        EXPECT_NEAR(back.orthographicScale(), copy->Scale(), kTolerance * copy->Scale());
    }
}

TEST(CameraSync, LeavesTheTargetWhenTheCameraIsBroken)
{
    Camera broken;
    broken.setProjectionType(Projection::Perspective);
    broken.setEyeAndCenter({}, {});
    ASSERT_FALSE(broken.isUsable());
    const Handle(Graphic3d_Camera) target = new Graphic3d_Camera();
    viewer::applyCamera(broken, *target);
    expectSame(Camera(), *target, kTolerance);
}
