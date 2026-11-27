// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/navigator.h"
#include "camera/views.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

using namespace kalve137::camera;

namespace {

class EmptyScene : public NavigationScene {
public:
    std::optional<Vec3> pick(const Camera&, const Viewport&, const Pixel&, bool) const override { return std::nullopt; }
    Vec3 gravityPoint(const Camera&, const Viewport&) const override { return {}; }
};

class WallScene : public NavigationScene {
public:
    std::optional<Vec3> pick(const Camera& camera, const Viewport& viewport, const Pixel& pixel, bool) const override
    {
        const Ray ray = camera.ray(pixel, viewport);
        if (std::abs(ray.direction.z) < 1e-9) {
            return std::nullopt;
        }
        const double t = -ray.origin.z / ray.direction.z;
        return t > 0.0 ? std::optional<Vec3>(ray.origin + ray.direction * t) : std::nullopt;
    }
    Vec3 gravityPoint(const Camera&, const Viewport&) const override { return {}; }
};

constexpr Viewport kViewport{800, 600, 1.0};

Camera started(Projection projection)
{
    Camera camera;
    camera.setProjectionType(projection);
    camera.setAspect(kViewport.aspect());
    homeView(camera);
    fitAround(camera, 100.0);
    return camera;
}

} // namespace

TEST(Navigator, PresetsMapExactButtonsAndModifiers)
{
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsCtrlPans, MiddleButton, NoModifier), Gesture::Orbit);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsCtrlPans, MiddleButton, Ctrl), Gesture::Pan);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsCtrlPans, MiddleButton, Shift), Gesture::Zoom);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddlePansShiftOrbits, MiddleButton, NoModifier), Gesture::Pan);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddlePansShiftOrbits, MiddleButton, Shift), Gesture::Orbit);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddlePansShiftOrbits, MiddleButton, Ctrl), Gesture::Zoom);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsShiftPans, MiddleButton, Shift), Gesture::Pan);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsShiftPans, MiddleButton, Ctrl), Gesture::Zoom);
    EXPECT_EQ(gestureFor(NavigationPreset::RightOrbitsCtrlPans, RightButton, NoModifier), Gesture::Orbit);
    EXPECT_EQ(gestureFor(NavigationPreset::RightOrbitsCtrlPans, RightButton, Ctrl), Gesture::Pan);
    EXPECT_EQ(gestureFor(NavigationPreset::RightOrbitsCtrlPans, MiddleButton, NoModifier), std::nullopt);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsCtrlPans, MiddleButton, Ctrl | Shift), std::nullopt);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsCtrlPans, MiddleButton | LeftButton, NoModifier), std::nullopt);
    EXPECT_EQ(gestureFor(NavigationPreset::MiddleOrbitsCtrlPans, LeftButton, NoModifier), std::nullopt);
}

TEST(Navigator, NothingPendingLeavesTheCamera)
{
    Navigator navigator;
    Camera camera = started(Projection::Orthographic);
    const Camera before = camera;
    EXPECT_FALSE(navigator.flush(camera, kViewport, EmptyScene()));
    EXPECT_EQ(camera.eye(), before.eye());
}

TEST(Navigator, IgnoresWheelStepsThatAreNotFinite)
{
    Navigator navigator;
    navigator.wheel({400, 300}, std::numeric_limits<double>::quiet_NaN());
    navigator.wheel({400, 300}, std::numeric_limits<double>::infinity());
    EXPECT_FALSE(navigator.hasPending());
}

TEST(Navigator, KeepsTheCameraWhenTheViewportIsEmpty)
{
    Navigator navigator;
    Camera camera = started(Projection::Orthographic);
    const Camera before = camera;
    navigator.wheel({400, 300}, 1.0);
    EXPECT_FALSE(navigator.flush(camera, Viewport{0, 600, 1.0}, EmptyScene()));
    EXPECT_EQ(camera.scale(), before.scale());
    EXPECT_FALSE(navigator.hasPending());
}

TEST(Navigator, WheelZoomsInAndReverseZoomsOut)
{
    for (const bool reverse : {false, true}) {
        Navigator navigator;
        Navigation navigation;
        navigation.reverseZoom = reverse;
        navigator.setNavigation(navigation);
        Camera camera = started(Projection::Orthographic);
        const double before = camera.scale();
        navigator.wheel({400, 300}, 1.0);
        ASSERT_TRUE(navigator.flush(camera, kViewport, EmptyScene()));
        EXPECT_EQ(camera.scale() < before, !reverse);
    }
}

TEST(Navigator, ZoomToCursorKeepsThePointUnderTheCursor)
{
    for (const Projection projection : {Projection::Orthographic, Projection::Perspective}) {
        Navigator navigator;
        Camera camera = started(projection);
        const WallScene scene;
        const Pixel cursor{600, 150};
        const std::optional<Vec3> before = scene.pick(camera, kViewport, cursor, true);
        ASSERT_TRUE(before);
        navigator.wheel(cursor, 2.0);
        ASSERT_TRUE(navigator.flush(camera, kViewport, scene));
        const ScreenPoint after = camera.project(*before, kViewport);
        EXPECT_NEAR(after.x, cursor.x, 1.5);
        EXPECT_NEAR(after.y, cursor.y, 1.5);
    }
}

TEST(Navigator, OrbitTurnsAroundTheGravityPointAndPanShifts)
{
    Navigator navigator;
    Camera camera = started(Projection::Orthographic);
    const double reach = distance(camera.center(), Vec3{});
    navigator.buttons({400, 300}, MiddleButton, NoModifier);
    navigator.move({500, 300}, MiddleButton, NoModifier);
    ASSERT_TRUE(navigator.flush(camera, kViewport, EmptyScene()));
    EXPECT_NEAR(distance(camera.center(), Vec3{}), reach, 1e-6);
    navigator.buttons({500, 300}, NoButton, NoModifier);

    const Vec3 center = camera.center();
    navigator.buttons({400, 300}, MiddleButton, Ctrl);
    navigator.move({450, 300}, MiddleButton, Ctrl);
    ASSERT_TRUE(navigator.flush(camera, kViewport, EmptyScene()));
    EXPECT_GT(distance(camera.center(), center), 1.0);
    EXPECT_TRUE(navigator.isGestureActive());
    navigator.reset();
    EXPECT_FALSE(navigator.isGestureActive());
}
