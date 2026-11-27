// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"

#include "app/shell/main_window.h"
#include "app/view/camera_commands.h"
#include "render/graphics_api.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "viewer/occt_view_widget.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QSize>
#include <QTest>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_ViewCube.hxx>
#include <NCollection_List.hxx>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>

namespace kalve137::uitest {

namespace {

constexpr QSize kWindowSize{800, 600};
constexpr QSize kCanvasSize{515, 441};
constexpr int kPinTries = 4;
constexpr int kReadyMs = 10000;
constexpr int kSettleMs = 300;
constexpr int kMarkerChange = 10;
constexpr int kMarkerWindowMargin = 2;

void pinCanvas(app::MainWindow& window, const QWidget& canvas)
{
    for (int i = 0; i < kPinTries && canvas.size() != kCanvasSize; ++i) {
        window.resize(window.size() + kCanvasSize - canvas.size());
        QApplication::processEvents();
    }
    EXPECT_EQ(canvas.size(), kCanvasSize) << "Pair window's canvas keeps one size on every platform";
}

void hideViewCube(viewer::OcctViewWidget& view)
{
    NCollection_List<Handle(AIS_InteractiveObject)> shown;
    view.aisContext()->DisplayedObjects(shown);
    for (const Handle(AIS_InteractiveObject)& object : shown) {
        if (object->IsKind(STANDARD_TYPE(AIS_ViewCube))) {
            view.aisContext()->Erase(object, false);
        }
    }
}

} // namespace

std::optional<std::string> viewportMissing()
{
    const app::StartupOptions startup = app::startupFromEnvironment();
    if (startup.view != app::ViewKind::New) {
        return "Runs with KALVE137_VIEW=new";
    }
    const render::GraphicsApi api = startup.graphicsApi.value_or(render::platformGraphicsApi());
    if (!render::canStart(api)) {
        return std::format("Viewport has no usable {} device here", render::graphicsApiName(api));
    }
    return std::nullopt;
}

std::unique_ptr<app::MainWindow> showPairWindow(app::ViewKind view)
{
    const std::optional<render::GraphicsApi> api =
        view == app::ViewKind::New ? app::startupFromEnvironment().graphicsApi : std::nullopt;
    auto window = std::make_unique<app::MainWindow>(app::StartupOptions{.view = view, .graphicsApi = api});
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->resize(kWindowSize);
    window->show();
    const bool settled = QTest::qWaitFor(
        [&] {
            if (view == app::ViewKind::New) {
                return window->viewport() == nullptr || window->viewport()->isReady() ||
                       window->viewport()->hasFailed();
            }
            return window->oldView()->isReady();
        },
        kReadyMs);
    EXPECT_TRUE(settled) << "Window's view did not start";
    if (QWidget* canvas = view == app::ViewKind::New ? static_cast<QWidget*>(window->viewport())
                                                     : static_cast<QWidget*>(window->oldView())) {
        pinCanvas(*window, *canvas);
    }
    return window;
}

QAction* actionNamed(app::MainWindow& window, const QString& text)
{
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().remove('&') == text) {
            return action;
        }
    }
    ADD_FAILURE() << "No action " << text.toStdString();
    return nullptr;
}

void showOnlyBodies(app::MainWindow& window, const QString& style)
{
    if (QAction* chosen = actionNamed(window, style)) {
        chosen->trigger();
    }
    if (QAction* grid = actionNamed(window, QStringLiteral("Show Grid")); grid != nullptr && grid->isChecked()) {
        grid->trigger();
    }
}

int gapOf(QRgb a, QRgb b)
{
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
}

int markerChangeAround(const QImage& before, const QImage& after, QPointF at)
{
    const double ratio = before.devicePixelRatio();
    const int half = static_cast<int>(std::ceil(((render::kMarkerSmallRing + kMarkerWindowMargin) * ratio) / 2.0));
    const QPoint center((at * ratio).toPoint());
    int changed = 0;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
            const QPoint pixel = center + QPoint(dx, dy);
            if (before.rect().contains(pixel) && gapOf(before.pixel(pixel), after.pixel(pixel)) >= kMarkerChange) {
                ++changed;
            }
        }
    }
    return changed;
}

QRgb median(const std::vector<QRgb>& colors)
{
    std::array<std::vector<int>, 3> channels;
    for (const QRgb color : colors) {
        channels[0].push_back(qRed(color));
        channels[1].push_back(qGreen(color));
        channels[2].push_back(qBlue(color));
    }
    std::array<int, 3> middle{};
    for (std::size_t i = 0; i < 3; ++i) {
        std::ranges::sort(channels.at(i));
        middle.at(i) = channels.at(i).at(channels.at(i).size() / 2);
    }
    return qRgb(middle[0], middle[1], middle[2]);
}

std::string hex(QRgb color)
{
    return QColor(color).name().toUpper().toStdString();
}

camera::Vec3 toVec(const snapshot::Point& point)
{
    return {point[0], point[1], point[2]};
}

void homeCamera(app::MainWindow& window)
{
    window.cameraCommands()->home(app::CameraCommands::Move::Jump);
}

Shots grabPair(app::MainWindow& oldWindow, app::MainWindow& newWindow, const std::function<void(app::MainWindow&)>& aim)
{
    render::Viewport& canvas = *newWindow.viewport();
    viewer::OcctViewWidget& old = *oldWindow.oldView();
    EXPECT_TRUE(
        QTest::qWaitFor([&] { return !newWindow.cameraCommands()->isAnimating() && !old.isAnimating(); }, kReadyMs))
        << "Camera kept moving before the pair was aimed";
    aim(newWindow);
    QTest::qWait(kSettleMs);
    const camera::Camera camera = canvas.camera();
    const camera::Viewport viewport = canvas.viewport();
    hideViewCube(old);
    old.setSyncedCamera(camera, viewport);
    QTest::qWait(kSettleMs);
    const snapshot::Color& background = canvas.viewportStyle().background;
    return {
        .old = old.grabFramebuffer().convertToFormat(QImage::Format_RGB32),
        .fresh = canvas.grabFramebuffer().convertToFormat(QImage::Format_RGB32),
        .camera = camera,
        .viewport = viewport,
        .scene = canvas.scene(),
        .background = QColor::fromRgbF(background.red, background.green, background.blue),
    };
}

void savePair(const Shots& shots, const std::filesystem::path& folder, const std::string& stem)
{
    shots.old.save(QString::fromStdU16String((folder / (stem + "-old.png")).u16string()));
    shots.fresh.save(QString::fromStdU16String((folder / (stem + "-new.png")).u16string()));
}

QPointF screenOf(const camera::Camera& camera, const camera::Viewport& viewport, const camera::Vec3& point)
{
    const camera::ScreenPoint at = camera.project(point, viewport);
    return {at.x, at.y};
}

void send(QWidget& widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget.mapToGlobal(at), button, buttons, {});
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
    if (auto* old = qobject_cast<viewer::OcctViewWidget*>(&widget)) {
        old->grabFramebuffer();
    } else if (auto* fresh = qobject_cast<render::Viewport*>(&widget)) {
        fresh->grabFramebuffer();
    }
}

void act(QWidget& view, QPointF at, bool click)
{
    send(view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    send(view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    if (click) {
        send(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        send(view, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    }
}

} // namespace kalve137::uitest
