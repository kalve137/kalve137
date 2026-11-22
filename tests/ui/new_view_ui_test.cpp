// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "app/view/camera_commands.h"
#include "app/view/new_view_navigation.h"
#include "camera/camera.h"
#include "render/graphics_api.h"
#include "render/rhi/rhi_view.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QTest>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace kalve137::uitest {
namespace {

using NewViewTest = UiTest;

constexpr int kColorTolerance = 2;
constexpr int kSettleMs = 200;
constexpr int kTransitionWaitMs = 3000;
constexpr int kWheelStep = 120;
constexpr double kMatrixTolerance = 1e-9;
constexpr double kDirectionTolerance = 1e-9;
constexpr QSize kWindowSize{800, 600};

void waitForCamera(app::CameraCommands& commands)
{
    EXPECT_TRUE(QTest::qWaitFor([&] { return !commands.isAnimating(); }, kTransitionWaitMs));
}

void expectSameMatrix(const camera::Mat4& ours, const camera::Mat4& theirs, const std::string& what)
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const double expected = theirs.at(row, column);
            EXPECT_NEAR(ours.at(row, column), expected, kMatrixTolerance * std::max(1.0, std::abs(expected)))
                << what << " at " << row << "," << column;
        }
    }
}

void expectFollowed(UiDriver& ui, const std::string& step)
{
    const camera::Camera& leading = ui.window().cameraCommands()->camera();
    const camera::Camera hidden = ui.window().oldView()->currentCamera();
    EXPECT_EQ(hidden.isOrthographic(), leading.isOrthographic()) << step;
    expectSameMatrix(hidden.view(), leading.view(), step + " hidden view");
    expectSameMatrix(hidden.projection(), leading.projection(), step + " hidden projection");
    expectSameMatrix(ui.newView()->camera().view(), leading.view(), step + " new view");
    expectSameMatrix(ui.newView()->camera().projection(), leading.projection(), step + " new projection");
}

void expectDirection(const camera::Camera& camera, const camera::Vec3& direction, const std::string& step)
{
    EXPECT_LT(camera::distance(camera.direction(), camera::normalized(direction)), kDirectionTolerance) << step;
}

void sendMouse(render::RhiView& canvas, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, canvas.mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}

#if defined(_WIN32)
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Metal;
#else
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Direct3D11;
#endif

bool sameColor(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) <= kColorTolerance && std::abs(a.green() - b.green()) <= kColorTolerance &&
           std::abs(a.blue() - b.blue()) <= kColorTolerance;
}

TEST_F(NewViewTest, ClearsToTheViewportColor)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
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

TEST_F(NewViewTest, ViewCommandsMoveTheCameraAndTheHiddenViewFollows)
{
    render::RhiView* canvas = ui->newView();
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

    const app::SnapshotNavigationScene scene(*canvas, *ui->window().oldView());
    const camera::Viewport viewport = *commands.viewport();
    const std::optional<camera::Vec3> hit =
        scene.pick(commands.camera(), viewport, {viewport.width / 2, viewport.height / 2}, true);
    ASSERT_TRUE(hit) << "the ray through the middle hits the part";
    EXPECT_LT(camera::dot(*hit - commands.camera().center(), commands.camera().direction()), 0.0);
    EXPECT_FALSE(scene.pick(commands.camera(), viewport, {2, 2}, true)) << "the corner ray misses";

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

TEST_F(NewViewTest, AnUnavailableApiShowsOneMessageInAPaintedWindow)
{
    allowProblem("graphics device");
    EXPECT_FALSE(render::canStart(kUnavailableApi));
    app::MainWindow window({.view = app::ViewKind::New, .graphicsApi = kUnavailableApi});
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(kWindowSize);
    window.show();
    QTest::qWait(kSettleMs);
    EXPECT_EQ(window.newView(), nullptr);
    const QList<QLabel*> messages = window.findChildren<QLabel*>(QStringLiteral("viewFailure"));
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front()->isVisible());
    EXPECT_EQ(window.centralWidget(), messages.front());
    const QString api = QString::fromUtf8(render::graphicsApiName(kUnavailableApi).data());
    EXPECT_TRUE(messages.front()->text().contains(api)) << messages.front()->text().toStdString();
    EXPECT_FALSE(window.menuBar()->grab().isNull());
}

} // namespace
} // namespace kalve137::uitest
