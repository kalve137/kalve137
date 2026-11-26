// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewport_ui_support.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "app/view/camera_commands.h"
#include "app/view/viewport_navigation.h"
#include "app/view/viewport_picker.h"
#include "camera/camera.h"
#include "camera/navigation.h"
#include "pick/scene_rays.h"
#include "render/rhi/viewport.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QTest>
#include <QWheelEvent>

#include <gp_Pnt.hxx>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace kalve137::uitest {
namespace {

constexpr int kWheelStep = 120;

TEST_F(ViewportTest, ClearsToTheViewportColor)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const QImage image = canvas->grabFramebuffer();
    ASSERT_FALSE(image.isNull());
    const QColor expected = app::theme::palette().viewport.canvas;
    for (const QPoint at : {QPoint(1, 1), image.rect().center(), QPoint(image.width() - 2, image.height() - 2)}) {
        const QColor seen = image.pixelColor(at);
        EXPECT_TRUE(sameColor(seen, expected))
            << "Pixel " << at.x() << "," << at.y() << " is " << seen.name().toStdString() << " instead of "
            << expected.name().toStdString();
    }
    ui->save("canvas");
}

TEST_F(ViewportTest, ViewCommandsMoveTheCameraAndTheHiddenViewFollows)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    app::CameraCommands& commands = *ui->window().cameraCommands();
    ui->window().openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" / "dodecahedron.k137");
    ui->settle();
    waitForCamera(commands);
    expectFollowed(*ui, "open");
    const double fitted = commands.camera().scale();
    ui->trigger("Front");
    waitForCamera(commands);
    expectDirection(commands.camera(), {0.0, 1.0, 0.0}, "front");
    expectFollowed(*ui, "front");

    ui->trigger("Home");
    waitForCamera(commands);
    expectDirection(commands.camera(), {-1.0, 1.0, -1.0}, "home");
    EXPECT_NEAR(commands.camera().scale(), fitted, 1e-6 * fitted);
    expectFollowed(*ui, "home");

    auto* session = ui->window().findChild<app::DesignSession*>();
    ASSERT_NE(session, nullptr);
    const app::SnapshotNavigationScene scene(*canvas);
    const camera::Viewport viewport = *commands.viewport();
    const camera::Pixel middlePixel{viewport.width / 2, viewport.height / 2};
    const std::optional<camera::Vec3> hit = scene.pick(commands.camera(), viewport, middlePixel, true);
    ASSERT_TRUE(hit) << "the depth under the middle finds the part";
    EXPECT_LT(camera::dot(*hit - commands.camera().center(), commands.camera().direction()), 0.0);
    EXPECT_FALSE(scene.pick(commands.camera(), viewport, {2, 2}, true)) << "the corner finds nothing";
    EXPECT_EQ(session->rays().builds(), 0U) << "navigation builds no ray scene";
    const camera::Ray ray = commands.camera().ray(middlePixel, viewport);
    const camera::Vec3 direction = camera::normalized(ray.direction);
    const std::optional<pick::Hit> traced =
        session->rays()
            .rays(canvas->scene())
            .first({ray.origin.x, ray.origin.y, ray.origin.z}, {direction.x, direction.y, direction.z});
    ASSERT_TRUE(traced);
    EXPECT_LT(camera::distance(*hit, ray.origin + direction * traced->distance),
              commands.camera().worldPerPixel(viewport))
        << "the depth at the pixel center matches the ray through its corner within a pixel";
    const std::size_t builds = session->rays().builds();
    const gp_Pnt hiddenGravity = ui->window().oldView()->gravityPoint();
    const camera::Vec3 gravity = scene.gravityPoint(commands.camera(), viewport);
    EXPECT_LT(camera::distance(gravity, {hiddenGravity.X(), hiddenGravity.Y(), hiddenGravity.Z()}), 1e-6)
        << "the snapshot gravity point matches the hidden view's";

    const QPointF middle(canvas->width() / 2.0, canvas->height() / 2.0);
    QWheelEvent wheel(middle, canvas->mapToGlobal(middle), QPoint(), QPoint(0, 2 * kWheelStep), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheel);
    EXPECT_LT(commands.camera().scale(), fitted * 0.99);
    expectFollowed(*ui, "wheel");

    sendMouse(*canvas, QEvent::MouseButtonPress, middle, Qt::MiddleButton, Qt::MiddleButton);
    sendMouse(*canvas, QEvent::MouseMove, middle + QPointF(40, -25), Qt::NoButton, Qt::MiddleButton);
    sendMouse(*canvas, QEvent::MouseButtonRelease, middle + QPointF(40, -25), Qt::MiddleButton, Qt::NoButton);
    EXPECT_GT(camera::distance(commands.camera().direction(), camera::normalized({-1.0, 1.0, -1.0})), 0.01);
    expectFollowed(*ui, "orbit");
    EXPECT_EQ(session->rays().builds(), builds) << "the wheel and the orbit build no ray scene";

    ui->trigger("Fit All");
    waitForCamera(commands);
    expectFollowed(*ui, "fit");
    ui->trigger("Home");
    waitForCamera(commands);
    EXPECT_NEAR(commands.camera().scale(), fitted, 1e-6 * fitted);

    ui->trigger("Perspective");
    waitForCamera(commands);
    EXPECT_FALSE(commands.camera().isOrthographic());
    expectFollowed(*ui, "perspective");
    ui->trigger("Orthographic");
    waitForCamera(commands);
    EXPECT_TRUE(commands.camera().isOrthographic());
    expectFollowed(*ui, "orthographic");

    QTest::qWait(kSettleMs);
    expectFollowed(*ui, "idle");
}

TEST_F(ViewportTest, OrbitsAroundTheDepthUnderTheCursor)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    app::CameraCommands& commands = *ui->window().cameraCommands();
    ui->window().openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" / "dodecahedron.k137");
    ui->settle();
    waitForCamera(commands);
    const camera::Navigation saved = commands.navigation();
    camera::Navigation aroundCursor = saved;
    aroundCursor.orbitAroundCursor = true;
    commands.setNavigation(aroundCursor);
    const Qt::MouseButton button =
        camera::gestureFor(saved.preset, camera::MiddleButton, camera::NoModifier) == camera::Gesture::Orbit
            ? Qt::MiddleButton
            : Qt::RightButton;
    const Qt::KeyboardModifiers modifiers;
    ASSERT_TRUE(camera::gestureFor(saved.preset,
                                   button == Qt::MiddleButton ? camera::MiddleButton : camera::RightButton,
                                   camera::NoModifier) == camera::Gesture::Orbit);
    const app::SnapshotNavigationScene scene(*canvas);

    const auto orbitFrom = [&](QPointF from) {
        const camera::Viewport viewport = *commands.viewport();
        const camera::Pixel pixel = camera::toPixel({from.x(), from.y()}, viewport);
        const std::optional<camera::Vec3> surface = scene.pick(commands.camera(), viewport, pixel, true);
        const camera::Vec3 pivot = surface ? *surface : scene.gravityPoint(commands.camera(), viewport);
        const camera::ScreenPoint before = commands.camera().project(pivot, viewport);
        const std::uint64_t passes = canvas->idPasses();
        QMouseEvent press(QEvent::MouseButtonPress, from, canvas->mapToGlobal(from), button, button, modifiers);
        QApplication::sendEvent(canvas, &press);
        EXPECT_LE(canvas->idPasses(), passes + 1) << "a press draws at most one id pass";
        for (int step = 1; step <= 3; ++step) {
            const QPointF to = from + QPointF(15.0 * step, -10.0 * step);
            QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, button, modifiers);
            QApplication::sendEvent(canvas, &move);
        }
        EXPECT_LE(canvas->idPasses(), passes + 1) << "the drag reads no more ids";
        QMouseEvent release(QEvent::MouseButtonRelease, from, canvas->mapToGlobal(from), button, Qt::NoButton,
                            modifiers);
        QApplication::sendEvent(canvas, &release);
        const camera::ScreenPoint after = commands.camera().project(pivot, viewport);
        EXPECT_LT(std::hypot(after.x - before.x, after.y - before.y), 1.0) << "the pivot stays put on screen";
        return surface.has_value();
    };
    const QPointF middle(canvas->width() / 2.0, canvas->height() / 2.0);
    EXPECT_TRUE(orbitFrom(middle + QPointF(12.0, 8.0))) << "the part lies under the cursor";
    EXPECT_FALSE(orbitFrom(QPointF(4.0, 4.0))) << "empty space orbits around the gravity point";
    commands.setNavigation(saved);
}

} // namespace
} // namespace kalve137::uitest
