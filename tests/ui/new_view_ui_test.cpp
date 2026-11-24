// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "../render/test_meshes.h"
#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "app/view/band_overlay.h"
#include "app/view/camera_commands.h"
#include "app/view/new_view_navigation.h"
#include "app/view/new_view_picker.h"
#include "app/view/pick.h"
#include "camera/camera.h"
#include "camera/navigation.h"
#include "camera/views.h"
#include "core/document.h"
#include "core/feature.h"
#include "geometry/exchange.h"
#include "geometry/shapes.h"
#include "pick/scene_rays.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/datums.h"
#include "render/draw_list.h"
#include "render/graphics_api.h"
#include "render/pick_id.h"
#include "render/rhi/rhi_backend.h"
#include "render/rhi/rhi_view.h"
#include "render/style.h"
#include "sketch/sketch.h"
#include "snapshot/body.h"
#include "snapshot/sketch.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

using NewViewTest = UiTest;

constexpr int kColorTolerance = 2;
constexpr int kBackendTolerance = 24;
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
constexpr std::array kOtherApis{render::GraphicsApi::Direct3D12, render::GraphicsApi::OpenGL};
#else
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Direct3D11;
constexpr std::array<render::GraphicsApi, 0> kOtherApis{};
#endif

bool sameColor(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) <= kColorTolerance && std::abs(a.green() - b.green()) <= kColorTolerance &&
           std::abs(a.blue() - b.blue()) <= kColorTolerance;
}

struct Comparison {
    bool sameSize = false;
    std::size_t body = 0;
    std::size_t different = 0;
};

render::Image cpuOf(const render::RhiView& canvas)
{
    render::ChunkSet chunks;
    chunks.update(canvas.scene(), canvas.viewportStyle());
    chunks.setHighlight(canvas.chunks().highlight());
    render::DrawList list = chunks.draw(canvas.frame());
    list.grid = canvas.grid().draw(canvas.camera(), canvas.viewport());
    list.datums = canvas.datumDraw();
    render::CpuBackend backend;
    render::Image cpu;
    backend.draw(list, 2, cpu);
    return cpu;
}

Comparison compare(const QImage& gpu, const render::Image& cpu)
{
    Comparison result;
    result.sameSize = gpu.width() == cpu.width && gpu.height() == cpu.height;
    if (!result.sameSize) {
        return result;
    }
    const QRgb background = cpu.at(0, 0);
    for (int y = 0; y < cpu.height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(gpu.constScanLine(y));
        for (int x = 0; x < cpu.width; ++x) {
            const QRgb ours = cpu.at(x, y);
            const QRgb theirs = line[x];
            result.body += ours != background ? 1 : 0;
            const int gap = std::max({std::abs(qRed(ours) - qRed(theirs)), std::abs(qGreen(ours) - qGreen(theirs)),
                                      std::abs(qBlue(ours) - qBlue(theirs))});
            result.different += gap > kBackendTolerance ? 1 : 0;
        }
    }
    return result;
}

QImage grabbed(render::RhiView& canvas)
{
    return canvas.grabFramebuffer().convertToFormat(QImage::Format_ARGB32);
}

class GridHidden {
public:
    explicit GridHidden(render::RhiView& canvas) : m_canvas(canvas), m_original(canvas.grid().settings())
    {
        render::GridSettings hidden = m_original;
        hidden.visible = false;
        m_canvas.setGrid(hidden);
    }
    GridHidden(const GridHidden&) = delete;
    GridHidden& operator=(const GridHidden&) = delete;
    ~GridHidden() { m_canvas.setGrid(m_original); }

private:
    render::RhiView& m_canvas;
    render::GridSettings m_original;
};

void expectShown(render::RhiView& canvas, const QImage& before, const std::string& step)
{
    const QImage gpu = grabbed(canvas);
    ASSERT_FALSE(gpu.isNull()) << step;
    const render::Image cpu = cpuOf(canvas);
    const Comparison comparison = compare(gpu, cpu);
    ASSERT_TRUE(comparison.sameSize) << step << ": " << gpu.width() << "x" << gpu.height() << " against " << cpu.width
                                     << "x" << cpu.height;
    const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
    EXPECT_GT(comparison.body, pixels / 50) << step;
    EXPECT_LT(comparison.different, pixels / 100) << step << ": " << comparison.different << " pixels differ";
    if (!before.isNull() && before.size() == gpu.size()) {
        EXPECT_NE(before, gpu) << step << " changed nothing on screen";
    }
}

TEST_F(NewViewTest, ClearsToTheViewportColor)
{
    render::RhiView* canvas = ui->newView();
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

TEST_F(NewViewTest, OrbitsAroundTheDepthUnderTheCursor)
{
    render::RhiView* canvas = ui->newView();
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

TEST_F(NewViewTest, DrawsTheTorusLikeTheCpuBackend)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    EXPECT_EQ(canvas->sampleCount(), 4);
    const snapshot::Point center{1234.5, -567.25, 89.0};
    const auto torus = std::make_shared<const snapshot::Body>(render::test::bodyOf(render::test::torus(center)));
    const snapshot::Scene scene{.bodies = {torus}, .origin = {1000.0, -1000.0, 0.0}};
    camera::Camera camera;
    ASSERT_TRUE(
        camera.setEyeAndCenter({center[0], center[1] - 300.0, center[2] + 400.0}, {center[0], center[1], center[2]}));
    ASSERT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    ASSERT_TRUE(camera.setScale(140.0));
    canvas->setScene(scene);
    canvas->setCamera(camera);
    const QImage gpu = canvas->grabFramebuffer().convertToFormat(QImage::Format_ARGB32);
    ASSERT_FALSE(gpu.isNull());
    EXPECT_TRUE(canvas->benchFrame()) << "the bench times multisampled frames";

    const render::Image cpu = cpuOf(*canvas);
    const Comparison comparison = compare(gpu, cpu);
    ASSERT_TRUE(comparison.sameSize);
    const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
    EXPECT_GT(comparison.body, pixels / 20) << "the torus fills part of the view";
    EXPECT_LT(comparison.different, pixels / 100)
        << comparison.different << " of " << pixels << " pixels differ by more than " << kBackendTolerance;
    const std::filesystem::path shot = ui->save("gpu");
    QImage cpuShot(cpu.width, cpu.height, QImage::Format_ARGB32);
    for (int y = 0; y < cpu.height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(cpuShot.scanLine(y));
        for (int x = 0; x < cpu.width; ++x) {
            line[x] = cpu.at(x, y);
        }
    }
    cpuShot.save(QString::fromStdU16String((shot.parent_path() / "cpu.png").u16string()));
}

std::size_t differing(const QImage& a, const QImage& b)
{
    if (a.size() != b.size()) {
        return static_cast<std::size_t>(std::max(a.width() * a.height(), b.width() * b.height()));
    }
    std::size_t count = 0;
    for (int y = 0; y < a.height(); ++y) {
        const auto* first = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* second = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int gap =
                std::max({std::abs(qRed(first[x]) - qRed(second[x])), std::abs(qGreen(first[x]) - qGreen(second[x])),
                          std::abs(qBlue(first[x]) - qBlue(second[x]))});
            count += gap > kColorTolerance ? 1 : 0;
        }
    }
    return count;
}

camera::Camera obliqueAt(const snapshot::Point& center, double scale)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center[0] + 300.0, center[1] - 200.0, center[2] + 250.0},
                                       {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(scale));
    return camera;
}

TEST_F(NewViewTest, DrawsEdgesLikeTheCpuBackend)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    const auto closed = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({990.0, -1010.0, -10.0}, {1010.0, -990.0, 10.0})));
    const auto open = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({1020.0, -1010.0, -10.0}, {1040.0, -990.0, 5.0}, false), {}, 1));
    canvas->setScene({.bodies = {closed, open}, .origin = origin, .hidden = {}});
    canvas->setCamera(obliqueAt({1015.0, -1000.0, 0.0}, 70.0));
    const render::ViewportStyle original = canvas->viewportStyle();
    for (const render::VisualStyle visual :
         {render::VisualStyle::ShadedWithEdges, render::VisualStyle::Wireframe, render::VisualStyle::VisibleEdges}) {
        render::ViewportStyle style = original;
        style.visualStyle = visual;
        canvas->setViewportStyle(style);
        const QImage gpu = grabbed(*canvas);
        const render::Image cpu = cpuOf(*canvas);
        const Comparison comparison = compare(gpu, cpu);
        ASSERT_TRUE(comparison.sameSize);
        const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
        EXPECT_GT(comparison.body, pixels / 500) << static_cast<int>(visual);
        EXPECT_LT(comparison.different, pixels / 200)
            << static_cast<int>(visual) << ": " << comparison.different << " of " << comparison.body
            << " drawn pixels differ by more than " << kBackendTolerance;
        ui->save("edges " + std::to_string(static_cast<int>(visual)));
    }
    canvas->setViewportStyle(original);
}

void expectHighlightsLikeTheCpuBackend(render::RhiView& canvas, const std::string& api, UiDriver& ui)
{
    const GridHidden gridOff(canvas);
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({990.0, -1010.0, -10.0}, {1010.0, -990.0, 10.0})));
    canvas.setScene({.bodies = {box}, .origin = origin, .hidden = {}});
    const auto item = [](render::PickKind kind, std::uint32_t index) {
        return render::PickedItem{.feature = 1, .part = 0, .kind = kind, .index = index};
    };
    render::Highlight highlight{
        .hovered = {item(render::PickKind::Face, 5)},
        .selected = {item(render::PickKind::Face, 2), item(render::PickKind::Face, 4), item(render::PickKind::Edge, 0)},
        .hoveredOutline = {},
        .selectedOutline = {},
    };
    for (std::uint32_t edge = 0; edge < box->mesh->edgeCount; ++edge) {
        (edge % 2 == 0 ? highlight.hoveredOutline : highlight.selectedOutline)
            .insert(item(render::PickKind::Edge, edge));
    }
    const std::array frames{obliqueAt({1000.0, -1000.0, 0.0}, 50.0), obliqueAt({1004.0, -998.0, 2.0}, 70.0),
                            obliqueAt({996.0, -1003.0, -2.0}, 40.0)};
    const render::ViewportStyle original = canvas.viewportStyle();
    for (const render::VisualStyle visual : {render::VisualStyle::ShadedWithEdges, render::VisualStyle::Shaded,
                                             render::VisualStyle::Wireframe, render::VisualStyle::VisibleEdges}) {
        render::ViewportStyle style = original;
        style.visualStyle = visual;
        canvas.setViewportStyle(style);
        for (std::size_t frame = 0; frame < frames.size(); ++frame) {
            const std::string where = std::format("{} style {} frame {}", api, static_cast<int>(visual), frame);
            canvas.setCamera(frames.at(frame));
            canvas.setHighlight({});
            const QImage plain = grabbed(canvas);
            canvas.setHighlight(highlight);
            const QImage gpu = grabbed(canvas);
            const render::Image cpu = cpuOf(canvas);
            const Comparison comparison = compare(gpu, cpu);
            ASSERT_TRUE(comparison.sameSize) << where;
            const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
            EXPECT_GT(differing(plain, gpu), comparison.body / 3) << where << ": the highlight shows";
            EXPECT_LT(comparison.different, pixels / 200)
                << where << ": " << comparison.different << " of " << comparison.body
                << " drawn pixels differ by more than " << kBackendTolerance;
            ui.save("highlight " + where);
        }
    }
    canvas.setHighlight({});
    canvas.setViewportStyle(original);
}

TEST_F(NewViewTest, DrawsHighlightsLikeTheCpuBackend)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    expectHighlightsLikeTheCpuBackend(*canvas, "main", *ui);
    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::RhiView view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        expectHighlightsLikeTheCpuBackend(view, std::string(render::graphicsApiName(api)), *ui);
    }
}

std::shared_ptr<const snapshot::Body> plateAt(const snapshot::Point& center, double size)
{
    snapshot::Mesh mesh = *render::test::grid(center, size, 6);
    const double half = size / 2.0;
    std::vector<snapshot::Point> rim;
    for (const auto& [u, v] : {std::pair{-1.0, -1.0}, std::pair{1.0, -1.0}, std::pair{1.0, 1.0}, std::pair{-1.0, 1.0},
                               std::pair{-1.0, -1.0}}) {
        rim.push_back({center[0] + (u * half), center[1] + (v * half), center[2]});
    }
    mesh.edges.push_back({.id = mesh.edgeCount++, .points = rim});
    return render::test::shared(render::test::bodyOf(std::make_shared<const snapshot::Mesh>(std::move(mesh))));
}

std::size_t insideChanges(const QImage& plain, const QImage& gridded)
{
    constexpr int kRim = 3;
    const QRgb background = plain.pixel(0, 0);
    const auto nearBackground = [&](int x, int y) {
        for (int ny = std::max(0, y - kRim); ny <= std::min(plain.height() - 1, y + kRim); ++ny) {
            for (int nx = std::max(0, x - kRim); nx <= std::min(plain.width() - 1, x + kRim); ++nx) {
                if (plain.pixel(nx, ny) == background) {
                    return true;
                }
            }
        }
        return false;
    };
    std::size_t count = 0;
    for (int y = 0; y < plain.height(); ++y) {
        for (int x = 0; x < plain.width(); ++x) {
            if (!sameColor(plain.pixelColor(x, y), gridded.pixelColor(x, y)) && !nearBackground(x, y)) {
                ++count;
            }
        }
    }
    return count;
}

void expectGridLikeTheCpuBackend(render::RhiView& canvas, const render::GridSettings& shown, const std::string& api,
                                 UiDriver& ui)
{
    const auto lookFrom = [](double x, double y, double z, double scale) {
        camera::Camera camera;
        EXPECT_TRUE(camera.setEyeAndCenter({1000.0 + x, -1000.0 + y, z}, {1000.0, -1000.0, 0.0}));
        EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
        EXPECT_TRUE(camera.setScale(scale));
        return camera;
    };
    const std::vector<std::pair<std::string, camera::Camera>> views{
        {"above", lookFrom(300.0, -200.0, 250.0, 90.0)},
        {"below", lookFrom(300.0, -200.0, -250.0, 90.0)},
        {"low orbit", lookFrom(300.0, -400.0, 20.0, 90.0)},
    };
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        for (const auto& [name, view] : views) {
            camera::Camera camera = view;
            camera.setProjectionType(projection);
            EXPECT_TRUE(camera.setScale(view.scale()));
            canvas.setCamera(camera);
            const std::string where = std::format("{} {} {}", api, name, static_cast<int>(projection));
            render::GridSettings hidden = shown;
            hidden.visible = false;
            canvas.setGrid(hidden);
            const QImage plain = grabbed(canvas);
            canvas.setGrid(shown);
            const QImage gpu = grabbed(canvas);
            const render::Image cpu = cpuOf(canvas);
            const Comparison comparison = compare(gpu, cpu);
            ASSERT_TRUE(comparison.sameSize) << where;
            const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
            EXPECT_GT(differing(plain, gpu), pixels / 50) << where << ": the grid shows";
            EXPECT_LT(comparison.different, pixels / 100)
                << where << ": " << comparison.different << " pixels differ by more than " << kBackendTolerance;
            if (name != "below") {
                EXPECT_EQ(insideChanges(plain, gpu), 0U) << where << ": the grid never shows inside a body";
            }
            ui.save("grid " + where);
        }
    }
}

TEST_F(NewViewTest, DrawsTheGridLikeTheCpuBackendAndHidesItInFaces)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({1015.0, -1010.0, 0.0}, {1035.0, -990.0, 20.0}), {}, 1));
    const auto plate = plateAt({990.0, -1000.0, 0.0}, 30.0);
    const snapshot::Scene scene{.bodies = {plate, box}, .origin = origin, .hidden = {}};
    canvas->setScene(scene);
    const render::GridSettings original = canvas->grid().settings();
    render::GridSettings shown = original;
    shown.visible = true;
    shown.extent.reset();
    expectGridLikeTheCpuBackend(*canvas, shown, "main", *ui);
    canvas->setGrid(original);

    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::RhiView view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        view.setScene(scene);
        expectGridLikeTheCpuBackend(view, shown, std::string(render::graphicsApiName(api)), *ui);
    }
}

TEST_F(NewViewTest, DrawsPlanesAndAxesLikeTheCpuBackend)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({-20.0, -10.0, 0.0}, {20.0, 10.0, 15.0}), {}, 1));
    const snapshot::Scene scene{.bodies = {box}, .origin = {}, .hidden = {}};
    const snapshot::Color plane{0.9F, 0.6F, 0.4F};
    const render::Datums datums{
        .originPlanes = {{.id = 0, .anchor = {}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}},
                         {.id = 0, .anchor = {}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 0.0, 1.0}},
                         {.id = 0, .anchor = {}, .u = {0.0, 1.0, 0.0}, .v = {0.0, 0.0, 1.0}}},
        .originAxes = {{.id = 0, .anchor = {}, .direction = {1.0, 0.0, 0.0}, .color = {0.85F, 0.3F, 0.3F}},
                       {.id = 0, .anchor = {}, .direction = {0.0, 1.0, 0.0}, .color = {0.3F, 0.7F, 0.3F}},
                       {.id = 0, .anchor = {}, .direction = {0.0, 0.0, 1.0}, .color = {0.3F, 0.5F, 0.9F}}},
        .planes = {{.id = 1, .anchor = {0.0, 0.0, 7.5}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}}},
        .axes = {{.id = 2, .anchor = {30.0, 0.0, 0.0}, .direction = {0.0, 1.0, 0.0}, .color = plane}},
        .planeColor = plane};
    const camera::Camera camera = obliqueAt({0.0, 0.0, 7.5}, 120.0);
    const auto check = [&](render::RhiView& view, const std::string& name) {
        view.setScene(scene);
        view.setCamera(camera);
        const QImage plain = grabbed(view);
        view.setDatums(datums);
        expectShown(view, plain, name);
    };
    check(*canvas, "main");
    canvas->setDatums({});

    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::RhiView view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        const GridHidden hidden(view);
        check(view, std::string(render::graphicsApiName(api)));
    }
}

TEST_F(NewViewTest, FitsASketchOnlyDesignToTheSketch)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const camera::Box drawn{{-150.0, -40.0, 0.0}, {250.0, 120.0, 0.0}};
    core::Document document;
    core::SketchFeature outline;
    sketch::Sketch& s = outline.sketch;
    const std::array corners{s.addPoint({drawn.min.x, drawn.min.y}), s.addPoint({drawn.max.x, drawn.min.y}),
                             s.addPoint({drawn.max.x, drawn.max.y}), s.addPoint({drawn.min.x, drawn.max.y})};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        s.addLine(corners[i], corners[(i + 1) % corners.size()]);
    }
    document.add(std::move(outline));
    ui->session().reset(document, {});
    ui->settle();
    ASSERT_EQ(canvas->sketches().size(), 1U);
    EXPECT_TRUE(canvas->scene().bodies.empty());

    app::CameraCommands& commands = *ui->window().cameraCommands();
    ui->trigger("Home");
    ui->trigger("Fit All");
    waitForCamera(commands);
    camera::Camera expected = commands.camera();
    ASSERT_TRUE(camera::fit(expected, drawn));
    EXPECT_NEAR(commands.camera().scale(), expected.scale(), 1e-6 * expected.scale());
    EXPECT_LT(camera::distance(commands.camera().center(), expected.center()), 1e-6 * expected.scale());
    const camera::Viewport viewport = *commands.viewport();
    double left = viewport.width;
    double right = 0.0;
    for (const camera::Vec3& corner : drawn.corners()) {
        const camera::ScreenPoint at = commands.camera().project(corner, viewport);
        EXPECT_GE(at.x, 0.0);
        EXPECT_LE(at.x, viewport.width);
        EXPECT_GE(at.y, 0.0);
        EXPECT_LE(at.y, viewport.height);
        left = std::min(left, at.x);
        right = std::max(right, at.x);
    }
    EXPECT_GT(right - left, viewport.width * 0.5) << "the sketch fills the view";
}

TEST_F(NewViewTest, ASketchOutsideTheBodiesLeavesTheDepthRange)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({-20.0, -10.0, 0.0}, {20.0, 10.0, 15.0}), {}, 1));
    const snapshot::Scene scene{.bodies = {box}, .origin = {}, .hidden = {}};
    canvas->setScene(scene);
    canvas->setCamera(obliqueAt({0.0, 0.0, 7.5}, 120.0));
    const camera::Mat4 projection = canvas->camera().projection();
    const render::Frame before = canvas->frame();

    const std::array distant{snapshot::Point{-400.0, 300.0, 2000.0}, snapshot::Point{500.0, -600.0, -1500.0}};
    snapshot::Sketch sketch;
    sketch.feature = 9;
    sketch.curves.push_back(
        {.id = 2, .kind = snapshot::SketchCurveKind::Line, .construction = false, .points = {distant[0], distant[1]}});
    canvas->setSketches({sketch});
    ASSERT_EQ(canvas->sketches().size(), 1U);
    const std::optional<camera::Box>& bounds = canvas->sceneBounds();
    ASSERT_TRUE(bounds);
    EXPECT_EQ(bounds->min.z, 0.0);
    EXPECT_EQ(bounds->max.z, 15.0);
    EXPECT_EQ(bounds->min.y, -10.0);
    expectSameMatrix(canvas->camera().projection(), projection, "projection");
    const render::Frame after = canvas->frame();
    EXPECT_EQ(after.viewProjection, before.viewProjection) << "the body depth range stays";
    EXPECT_EQ(after.exactViewProjection, before.exactViewProjection);
    for (const snapshot::Point& point : distant) {
        const std::array<float, 16>& m = after.wideViewProjection;
        std::array<double, 4> clip{};
        for (std::size_t row = 0; row < clip.size(); ++row) {
            clip.at(row) =
                (m.at(row) * point[0]) + (m.at(row + 4) * point[1]) + (m.at(row + 8) * point[2]) + m.at(row + 12);
        }
        const double depth = clip[2] / clip[3];
        EXPECT_GE(depth, -1.0) << "the sketch clamps into the depth range instead of clipping";
        EXPECT_LE(depth, 1.0);
    }
    const std::optional<camera::Box> fitted = canvas->fitBounds(60.0);
    ASSERT_TRUE(fitted);
    EXPECT_EQ(fitted->max.z, 2000.0);
    EXPECT_EQ(fitted->min.y, -600.0);

    canvas->setSketches({});
    EXPECT_EQ(canvas->frame().wideViewProjection, before.wideViewProjection);
}

TEST_F(NewViewTest, AChangeShowsInTheNextFrame)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const snapshot::Point center{500.0, 200.0, 50.0};
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::box({center[0] - 10.0, center[1] - 10.0, center[2] - 10.0},
                                               {center[0] + 10.0, center[1] + 10.0, center[2] + 10.0})));
    const auto torus = render::test::shared(
        render::test::bodyOf(render::test::torus({center[0] + 40.0, center[1], center[2]}, 64, 32), {}, 1));
    const snapshot::Point origin{400.0, 200.0, 0.0};
    canvas->setCamera(obliqueAt({center[0] + 20.0, center[1], center[2]}, 80.0));
    canvas->setScene({.bodies = {box}, .origin = origin, .hidden = {}});
    expectShown(*canvas, {}, "the first scene");
    QImage previous = grabbed(*canvas);

    const auto step = [&](const std::string& what) {
        expectShown(*canvas, previous, what);
        previous = grabbed(*canvas);
    };
    canvas->setScene({.bodies = {box, torus}, .origin = origin, .hidden = {}});
    step("a body added");
    const auto red = render::test::shared(
        render::test::bodyOf(box->mesh, {.color = snapshot::Color{0.9F, 0.1F, 0.1F}, .faces = {}}));
    canvas->setScene({.bodies = {red, torus}, .origin = origin, .hidden = {}});
    step("a recolor");
    canvas->setScene({.bodies = {torus}, .origin = origin, .hidden = {red}});
    step("a hide");
    canvas->setScene({.bodies = {red, torus}, .origin = origin, .hidden = {}});
    step("a show");
    const render::ViewportStyle themed = canvas->viewportStyle();
    render::ViewportStyle light = themed;
    light.body = {0.85F, 0.8F, 0.3F};
    light.background = {0.9F, 0.92F, 0.95F};
    canvas->setViewportStyle(light);
    step("a theme change");

    const snapshot::Point distant{20400.0, 200.0, 0.0};
    canvas->setScene({.bodies = {red, torus}, .origin = distant, .hidden = {}});
    expectShown(*canvas, {}, "an origin change");
    const QImage moved = grabbed(*canvas);
    EXPECT_LT(differing(moved, previous), static_cast<std::size_t>(moved.width() * moved.height()) / 200)
        << "a new origin draws the same picture";
    previous = moved;

    render::ViewportStyle denser = canvas->viewportStyle();
    denser.pixelRatio = 2.0;
    canvas->setViewportStyle(denser);
    expectShown(*canvas, {}, "a pixel ratio change");
    EXPECT_EQ(canvas->chunks().draw({}).pixelRatio, 2.0) << "the next frame draws at the new ratio";
    canvas->setViewportStyle(themed);
    expectShown(*canvas, {}, "the theme and the pixel ratio back");
    EXPECT_EQ(canvas->chunks().draw({}).pixelRatio, themed.pixelRatio);
    ui->save("staleness");
}

std::size_t displayedObjects(viewer::OcctViewWidget& view)
{
    NCollection_List<Handle(AIS_InteractiveObject)> shown;
    view.aisContext()->DisplayedObjects(shown);
    return static_cast<std::size_t>(shown.Size());
}

TEST_F(NewViewTest, HideAndShowRewriteOnlyThePalette)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    constexpr double kSide = 20.0;
    constexpr double kGap = 40.0;
    std::vector<geometry::StepPart> parts;
    for (int i = 0; i < 2; ++i) {
        geometry::StepPart part;
        part.name = "Box " + std::to_string(i);
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(kGap * i, 0.0, 0.0));
        part.shape = geometry::makeBox(kSide, kSide, kSide).Moved(TopLoc_Location(shift));
        parts.push_back(std::move(part));
    }
    core::Document document;
    document.add(core::ImportFeature{"boxes.step", std::move(parts)});
    app::DesignSession& session = ui->session();
    session.reset(std::move(document), {});
    ui->window().cameraCommands()->fit(app::CameraCommands::Move::Jump);
    ui->settle(kSettleMs);
    ASSERT_EQ(session.model().bodies.size(), 2U);
    const core::BodyKey second = session.model().bodies[1].key();
    const auto bodies = canvas->scene().bodies;
    const QImage before = grabbed(*canvas);
    session.rays().rays(canvas->scene());
    const std::size_t rayBuilds = session.rays().builds();
    const std::uint64_t meshBuilds = canvas->chunks().meshBuilds();
    viewer::OcctViewWidget& old = *ui->window().oldView();
    const std::size_t displayed = displayedObjects(old);
    const pick::Point above{kGap + kSide / 2.0, kSide / 2.0, kGap * 10.0};
    const pick::Point down{0.0, 0.0, -1.0};

    session.setBodyVisible(second, false);
    EXPECT_EQ(canvas->scene().bodies.size(), 1U);
    EXPECT_EQ(canvas->scene().hidden.size(), 1U);
    EXPECT_EQ(canvas->chunks().meshBuilds(), meshBuilds) << "a hide rewrites palettes only";
    expectShown(*canvas, before, "a hide");
    EXPECT_FALSE(session.rays().rays(canvas->scene()).first(above, down)) << "a hidden body takes no ray";
    EXPECT_EQ(session.rays().builds(), rayBuilds) << "a hide keeps the ray scene";
    EXPECT_EQ(displayedObjects(old), displayed - 1) << "the old view erases only the hidden body";

    session.setBodyVisible(second, true);
    EXPECT_EQ(canvas->scene().bodies, bodies) << "a show puts the body back in model order";
    EXPECT_TRUE(canvas->scene().hidden.empty());
    EXPECT_EQ(canvas->chunks().meshBuilds(), meshBuilds);
    EXPECT_EQ(differing(grabbed(*canvas), before), 0U) << "a hide and a show leave the same frame";
    EXPECT_TRUE(session.rays().rays(canvas->scene()).first(above, down));
    EXPECT_EQ(session.rays().builds(), rayBuilds);
    EXPECT_EQ(displayedObjects(old), displayed);
}

std::size_t backFaceLeakAllowed(const render::RhiView& view, const QImage& image)
{
    const bool leaks = view.sampleCount() > 1;
    return leaks ? static_cast<std::size_t>(image.width() * image.height()) / 2000 : 0;
}

std::optional<std::pair<std::size_t, std::size_t>> cullingGap(render::GraphicsApi api, QSize size,
                                                              const render::ViewportStyle& style,
                                                              const snapshot::Scene& scene,
                                                              const camera::Camera& camera)
{
    if (!render::canStart(api)) {
        return std::nullopt;
    }
    render::RhiView view(api);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(size);
    view.show();
    view.setViewportStyle(style);
    view.setCamera(camera);
    view.setScene(scene);
    const QImage culled = grabbed(view);
    render::ViewportStyle unculled = style;
    unculled.cullClosed = false;
    view.setViewportStyle(unculled);
    return std::pair{differing(culled, grabbed(view)), backFaceLeakAllowed(view, culled)};
}

TEST_F(NewViewTest, CullingClosedBodiesChangesNoPixel)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const auto box =
        render::test::shared(render::test::bodyOf(render::test::box({-10.0, -10.0, -10.0}, {10.0, 10.0, 10.0})));
    const auto torus = render::test::shared(render::test::bodyOf(render::test::torus({45.0, 0.0, 0.0}, 64, 32), {}, 1));
    const snapshot::Scene scene{.bodies = {box, torus}, .origin = {}, .hidden = {}};
    const camera::Camera camera = obliqueAt({20.0, 0.0, 0.0}, 90.0);
    canvas->setCamera(camera);
    canvas->setScene(scene);
    const render::ViewportStyle original = canvas->viewportStyle();
    ASSERT_TRUE(original.cullClosed);
    render::ViewportStyle style = original;
    style.visualStyle = render::VisualStyle::Shaded;
    canvas->setViewportStyle(style);
    expectShown(*canvas, {}, "closed bodies culled");

    const QImage culled = grabbed(*canvas);
    render::ViewportStyle unculled = style;
    unculled.cullClosed = false;
    canvas->setViewportStyle(unculled);
    const QImage all = grabbed(*canvas);
    canvas->setViewportStyle(original);
    EXPECT_LE(differing(culled, all), backFaceLeakAllowed(*canvas, culled))
        << "the GPU winds front faces counterclockwise like the CPU backend";

    for (const render::GraphicsApi api : kOtherApis) {
        if (const auto gap = cullingGap(api, canvas->size(), style, scene, camera)) {
            EXPECT_LE(gap->first, gap->second) << render::graphicsApiName(api);
        }
    }
}

using IdCache = std::map<std::uint32_t, std::optional<render::PickedItem>>;

std::optional<render::PickedItem> itemOf(std::uint32_t id, const render::ChunkSet& chunks,
                                         const std::vector<std::uint64_t>& places, IdCache& cache)
{
    const auto [found, added] = cache.try_emplace(id);
    if (added) {
        const render::PickId decoded = render::decodePickId(id);
        if (decoded.kind != render::PickKind::None && decoded.chunk < places.size()) {
            found->second = chunks.itemOf(places[decoded.chunk], decoded.kind, decoded.entry);
        }
    }
    return found->second;
}

render::Frame frameOf(const render::RhiView& canvas)
{
    return render::makeFrame(canvas.camera(), canvas.viewport(), canvas.scene().origin,
                             canvas.viewportStyle().background);
}

QPointF devicePixelOf(const render::RhiView& canvas, const snapshot::Point& point)
{
    const render::Frame frame = frameOf(canvas);
    const snapshot::Point& origin = canvas.scene().origin;
    const std::array<float, 16>& m = frame.viewProjection;
    std::array<double, 4> clip{};
    for (std::size_t r = 0; r < clip.size(); ++r) {
        clip[r] = (static_cast<double>(m[r]) * (point[0] - origin[0])) +
                  (static_cast<double>(m[r + 4]) * (point[1] - origin[1])) +
                  (static_cast<double>(m[r + 8]) * (point[2] - origin[2])) + static_cast<double>(m[r + 12]);
    }
    return {(clip[0] / clip[3] + 1.0) * 0.5 * frame.width, (1.0 - clip[1] / clip[3]) * 0.5 * frame.height};
}

QPointF logicalPixelOf(const render::RhiView& canvas, const snapshot::Point& point)
{
    return devicePixelOf(canvas, point) / canvas.viewport().pixelRatio;
}

void expectMarkersFollowTheCamera(render::RhiView& canvas, const std::string& api)
{
    constexpr int kSearch = 14;
    constexpr double kMarkerSpacing = 6.0;
    const GridHidden gridOff(canvas);
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    canvas.setScene({.bodies = {}, .origin = origin, .hidden = {}});
    const std::array shapes{render::MarkerShape::Ball, render::MarkerShape::Ring, render::MarkerShape::Plus,
                            render::MarkerShape::RingPlus, render::MarkerShape::Star};
    std::vector<render::MarkerItem> items;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const double offset = (static_cast<double>(i) - 2.0) * kMarkerSpacing;
        items.push_back({.anchor = {origin[0] + offset, origin[1] - offset, origin[2]},
                         .shape = shapes.at(i),
                         .scale = render::MarkerScale::Medium,
                         .color = {1.0F, 0.8F, 0.2F},
                         .flags = render::kMarkerTopmost,
                         .pick = 0});
    }
    canvas.setMarkers(items);
    const std::array frames{obliqueAt({1000.0, -1000.0, 0.0}, 50.0), obliqueAt({1004.0, -998.0, 2.0}, 70.0),
                            obliqueAt({996.0, -1003.0, -2.0}, 40.0)};
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        canvas.setCamera(frames.at(frame));
        const QImage image = grabbed(canvas);
        ASSERT_FALSE(image.isNull());
        const QRgb background = image.pixel(0, 0);
        for (const render::MarkerItem& item : items) {
            const std::string where = std::format("{} frame {} shape {}", api, frame, static_cast<int>(item.shape));
            const QPointF at = devicePixelOf(canvas, {item.anchor.x, item.anchor.y, item.anchor.z});
            const int x = static_cast<int>(std::floor(at.x()));
            const int y = static_cast<int>(std::floor(at.y()));
            QRect box;
            for (int row = std::max(0, y - kSearch); row <= std::min(image.height() - 1, y + kSearch); ++row) {
                for (int column = std::max(0, x - kSearch); column <= std::min(image.width() - 1, x + kSearch);
                     ++column) {
                    if (!sameColor(image.pixelColor(column, row), QColor(background))) {
                        box = box.united(QRect(column, row, 1, 1));
                    }
                }
            }
            ASSERT_FALSE(box.isNull()) << where << " draws";
            EXPECT_NEAR((box.left() + box.right()) / 2.0, x, 1.0) << where << " sits on its anchor";
            EXPECT_NEAR((box.top() + box.bottom()) / 2.0, y, 1.0) << where << " sits on its anchor";
        }
    }
    canvas.setMarkers({});
}

TEST_F(NewViewTest, DrawsMarkersOnTheirAnchorsAcrossCameraMovesOnEveryApi)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    expectMarkersFollowTheCamera(*canvas, "main");
    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::RhiView view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        expectMarkersFollowTheCamera(view, std::string(render::graphicsApiName(api)));
    }
}

QPointF toward(QPointF from, QPointF to, double distance)
{
    const QPointF step = to - from;
    return from + (step / std::hypot(step.x(), step.y()) * distance);
}

void expectCpuIds(render::RhiView& canvas, const std::string& api)
{
    const render::IdStyle style{.points = true};
    const camera::Viewport port = canvas.viewport();
    const std::optional<render::IdImage> gpu = canvas.readIds(QRect(0, 0, port.width, port.height), style);
    ASSERT_TRUE(gpu) << api;

    render::ChunkSet chunks;
    chunks.update(canvas.scene(), canvas.viewportStyle());
    const render::DrawList list = chunks.pickList(frameOf(canvas));
    std::vector<std::uint64_t> cpuPlaces;
    cpuPlaces.reserve(list.chunks.size());
    for (const render::Chunk& chunk : list.chunks) {
        cpuPlaces.push_back(chunk.id);
    }
    render::IdImage cpu;
    render::CpuBackend backend;
    backend.drawIds(list, style, cpu);
    ASSERT_EQ(gpu->width, cpu.width) << api;
    ASSERT_EQ(gpu->height, cpu.height) << api;

    IdCache gpuCache;
    IdCache cpuCache;
    std::size_t body = 0;
    std::size_t different = 0;
    std::size_t kinds = 0;
    float depthGap = 0.0F;
    for (int y = 0; y < cpu.height; ++y) {
        for (int x = 0; x < cpu.width; ++x) {
            const std::optional<render::PickedItem> ours = itemOf(cpu.idAt(x, y), chunks, cpuPlaces, cpuCache);
            const std::optional<render::PickedItem> theirs =
                itemOf(gpu->idAt(x, y), canvas.chunks(), canvas.backend()->idChunks(), gpuCache);
            body += ours ? 1 : 0;
            different += ours != theirs ? 1 : 0;
            kinds += ours && theirs && ours->kind != theirs->kind ? 1 : 0;
            if (ours && ours == theirs) {
                depthGap = std::max(depthGap, std::abs(cpu.depthAt(x, y) - gpu->depthAt(x, y)));
            }
        }
    }
    const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
    EXPECT_GT(body, pixels / 20) << api << ": the box and the torus fill part of the view";
    EXPECT_LT(different, body / 50) << api << ": " << different << " of " << body << " picked pixels differ";
    EXPECT_LT(kinds, body / 100) << api;
    EXPECT_LT(depthGap, 2e-4F) << api << ": the picked depth leaves out the face bias";
    std::set<render::PickKind> seen;
    for (const auto& [id, item] : gpuCache) {
        if (item) {
            seen.insert(item->kind);
        }
    }
    EXPECT_EQ(seen.size(), 3U) << api << ": faces, edges and points all reach the id buffer";
}

TEST_F(NewViewTest, TheIdPassMatchesTheCpuIds)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({990.0, -1010.0, -10.0}, {1010.0, -990.0, 10.0})));
    const auto torus =
        render::test::shared(render::test::bodyOf(render::test::torus({1060.0, -1000.0, 0.0}, 64, 32), {}, 1));
    const snapshot::Scene scene{.bodies = {box, torus}, .origin = origin, .hidden = {}};
    const camera::Camera camera = obliqueAt({1030.0, -1000.0, 0.0}, 110.0);
    canvas->setScene(scene);
    canvas->setCamera(camera);
    ASSERT_FALSE(grabbed(*canvas).isNull());
    expectCpuIds(*canvas, "the main view");

    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::RhiView view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        view.setCamera(camera);
        view.setScene(scene);
        ASSERT_FALSE(grabbed(view).isNull());
        expectCpuIds(view, std::string(render::graphicsApiName(api)));
    }
}

TEST_F(NewViewTest, PicksOnDemandAndOnlyWhenStale)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const auto mesh = render::test::edgedBox({-10.0, -10.0, -10.0}, {10.0, 10.0, 10.0});
    const auto box = render::test::shared(render::test::bodyOf(mesh));
    canvas->setScene({.bodies = {box}, .origin = {}, .hidden = {}});
    const camera::Camera camera = obliqueAt({}, 60.0);
    canvas->setCamera(camera);
    ASSERT_FALSE(grabbed(*canvas).isNull());
    const std::uint64_t passes = canvas->idPasses();
    const render::PickKinds facesAndEdges{.faces = true, .edges = true, .points = false};
    const render::PickKinds everything{.faces = true, .edges = true, .points = true};
    constexpr std::uint32_t kTopFace = 5;

    const QPointF topCenter = logicalPixelOf(*canvas, {0.0, 0.0, 10.0});
    const std::optional<render::IdPick> face = canvas->pickAt(topCenter, facesAndEdges);
    ASSERT_TRUE(face);
    EXPECT_EQ(face->item,
              (render::PickedItem{.feature = 1, .part = 0, .kind = render::PickKind::Face, .index = kTopFace}));
    EXPECT_EQ(canvas->idPasses(), passes + 1) << "the first pick draws the id pass";

    std::uint32_t frontTop = 0;
    for (const snapshot::Edge& edge : mesh->edges) {
        if (edge.points.front() == snapshot::Point{10.0, -10.0, 10.0} &&
            edge.points.back() == snapshot::Point{10.0, 10.0, 10.0}) {
            frontTop = edge.id;
        }
    }
    const QPointF onEdge = logicalPixelOf(*canvas, {10.0, 0.0, 10.0});
    const std::optional<render::IdPick> edge = canvas->pickAt(toward(onEdge, topCenter, 3.0), facesAndEdges);
    ASSERT_TRUE(edge);
    EXPECT_EQ(edge->item,
              (render::PickedItem{.feature = 1, .part = 0, .kind = render::PickKind::Edge, .index = frontTop}))
        << "an edge 3 px away beats the face under the cursor";
    EXPECT_EQ(canvas->idPasses(), passes + 1) << "an unchanged view reads the kept id pass";

    const QPointF corner = toward(logicalPixelOf(*canvas, {10.0, -10.0, 10.0}), topCenter, 2.0);
    const std::optional<render::IdPick> point = canvas->pickAt(corner, everything, {.points = true});
    ASSERT_TRUE(point);
    EXPECT_EQ(point->item.kind, render::PickKind::Point);
    EXPECT_EQ(point->item.index, 5U);
    EXPECT_EQ(canvas->idPasses(), passes + 2) << "points draw in a pass of their own";

    camera::Camera turned = camera;
    ASSERT_TRUE(turned.setScale(70.0));
    canvas->setCamera(turned);
    EXPECT_TRUE(canvas->pickAt(logicalPixelOf(*canvas, {0.0, 0.0, 10.0}), facesAndEdges));
    EXPECT_EQ(canvas->idPasses(), passes + 3) << "a camera change makes the id pass stale";

    canvas->setScene({.bodies = {}, .origin = {}, .hidden = {box}});
    EXPECT_FALSE(canvas->pickAt(logicalPixelOf(*canvas, {0.0, 0.0, 10.0}), everything, {.points = true}))
        << "a hidden body picks nothing";
    EXPECT_EQ(canvas->idPasses(), passes + 4);
    EXPECT_FALSE(canvas->pickAt(QPointF(2.0, 2.0), everything));
}

TEST_F(NewViewTest, ABodyBroughtNearerWithoutACameraMoveDrawsWholeAndPicks)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const snapshot::Point low{-10.0, -10.0, -10.0};
    const snapshot::Point high{10.0, 10.0, 10.0};
    const snapshot::Point offset{60.0, -40.0, 50.0};
    const auto shifted = [&](const snapshot::Point& point) {
        return snapshot::Point{point[0] + offset[0], point[1] + offset[1], point[2] + offset[2]};
    };
    const auto base = render::test::shared(render::test::bodyOf(render::test::edgedBox(low, high)));
    const auto moved = render::test::shared(render::test::bodyOf(render::test::edgedBox(shifted(low), shifted(high))));
    const auto grown = render::test::shared(render::test::bodyOf(render::test::edgedBox(low, shifted(high))));
    const auto copy =
        render::test::shared(render::test::bodyOf(render::test::edgedBox(shifted(low), shifted(high)), {}, 1));
    struct Change {
        const char* name;
        snapshot::Scene scene;
        snapshot::Point low;
        snapshot::Point high;
        int part;
    };
    const std::vector<Change> changes{
        {"moved", {.bodies = {moved}, .origin = {}, .hidden = {}}, shifted(low), shifted(high), 0},
        {"grown", {.bodies = {grown}, .origin = {}, .hidden = {}}, low, shifted(high), 0},
        {"patterned", {.bodies = {base, copy}, .origin = {}, .hidden = {}}, shifted(low), shifted(high), 1},
    };
    const render::PickKinds solids{.faces = true, .edges = true, .points = false};
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        for (const Change& change : changes) {
            SCOPED_TRACE(std::string(change.name) +
                         (projection == camera::Projection::Orthographic ? " orthographic" : " perspective"));
            canvas->setScene({.bodies = {base}, .origin = {}, .hidden = {}});
            camera::Camera camera = obliqueAt({}, 160.0);
            camera.setProjectionType(projection);
            canvas->setCamera(camera);
            ASSERT_FALSE(grabbed(*canvas).isNull());
            canvas->setScene(change.scene);
            const QImage image = grabbed(*canvas);
            ASSERT_FALSE(image.isNull());

            const camera::Box box{{change.low[0], change.low[1], change.low[2]},
                                  {change.high[0], change.high[1], change.high[2]}};
            camera::Vec3 nearest = box.min;
            for (const camera::Vec3& corner : box.corners()) {
                if (camera::dot(corner - nearest, canvas->camera().direction()) < 0.0) {
                    nearest = corner;
                }
            }
            const camera::Vec3 center = box.center();
            const auto inward = [](double from, double to) { return from + std::copysign(1.5, to - from); };
            const QPointF inside = devicePixelOf(
                *canvas, {inward(nearest.x, center.x), inward(nearest.y, center.y), inward(nearest.z, center.z)});
            const QPoint pixel(static_cast<int>(std::floor(inside.x())), static_cast<int>(std::floor(inside.y())));
            ASSERT_TRUE(image.rect().contains(pixel));
            EXPECT_FALSE(sameColor(image.pixelColor(pixel), QColor::fromRgbF(canvas->viewportStyle().background.red,
                                                                             canvas->viewportStyle().background.green,
                                                                             canvas->viewportStyle().background.blue)))
                << "the nearest corner draws";
            const std::optional<std::vector<render::PickedItem>> items =
                canvas->itemsIn(QRect(pixel, QSize(1, 1)), false, solids);
            ASSERT_TRUE(items);
            ASSERT_EQ(items->size(), 1U);
            EXPECT_EQ(items->front().part, change.part) << "the nearest corner picks";
        }
    }
}

void click(render::RhiView& canvas, QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, canvas.mapToGlobal(position), Qt::LeftButton, Qt::LeftButton,
                      modifiers);
    QApplication::sendEvent(&canvas, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, canvas.mapToGlobal(position), Qt::LeftButton,
                        Qt::NoButton, modifiers);
    QApplication::sendEvent(&canvas, &release);
    QApplication::processEvents();
}

TEST_F(NewViewTest, HoverAndClicksSelectThroughTheIdPass)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    ui->window().openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" / "dodecahedron.k137");
    ui->settle();
    waitForCamera(*ui->window().cameraCommands());
    QSignalSpy hovers(picker, &app::PickSource::hoverChanged);
    QSignalSpy selections(picker, &app::PickSource::selectionChanged);
    EXPECT_EQ(picker->kinds(), (render::PickKinds{.faces = true, .edges = true, .points = false}));

    const QPointF middle(canvas->width() / 2.0, canvas->height() / 2.0);
    sendMouse(*canvas, QEvent::MouseMove, middle, Qt::NoButton, Qt::NoButton);
    EXPECT_EQ(hovers.count(), 0) << "signals arrive queued, like the old view's";
    QApplication::processEvents();
    EXPECT_EQ(hovers.count(), 1);
    const std::optional<app::Pick> hovered = picker->hovered();
    ASSERT_TRUE(hovered);
    EXPECT_EQ(hovered->kind, app::Pick::Kind::Face);
    ASSERT_FALSE(hovered->shape.IsNull());
    EXPECT_EQ(hovered->shape.ShapeType(), TopAbs_FACE);
    ASSERT_TRUE(hovered->point);
    const QPointF at = logicalPixelOf(*canvas, {hovered->point->X(), hovered->point->Y(), hovered->point->Z()});
    EXPECT_LT(std::hypot(at.x() - middle.x(), at.y() - middle.y()), 1.5) << "the point lies under the cursor";
    EXPECT_EQ(canvas->chunks().highlight().hovered.size(), 1U);
    sendMouse(*canvas, QEvent::MouseMove, middle + QPointF(0.5, 0.0), Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    EXPECT_EQ(hovers.count(), 1) << "a move over the same face announces nothing";

    click(*canvas, middle);
    EXPECT_EQ(selections.count(), 1);
    ASSERT_EQ(picker->selected().size(), 1U);
    EXPECT_TRUE(picker->selected()[0].shape.IsSame(hovered->shape));
    EXPECT_EQ(canvas->chunks().highlight().selected.size(), 1U);

    std::optional<QPointF> other;
    for (int step = 0; step < canvas->width() / 8 && !other; ++step) {
        const double x = middle.x() + (4.0 * step);
        const std::optional<render::IdPick> found =
            canvas->pickAt({x, middle.y()}, {.faces = true, .edges = false, .points = false});
        if (found && found->item != *canvas->chunks().highlight().selected.begin()) {
            other = QPointF(x + 8.0, middle.y());
        }
    }
    ASSERT_TRUE(other);
    click(*canvas, *other, Qt::ShiftModifier);
    EXPECT_EQ(picker->selected().size(), 2U) << "Shift adds";
    click(*canvas, middle, Qt::ShiftModifier);
    ASSERT_EQ(picker->selected().size(), 1U) << "Shift toggles off";
    EXPECT_FALSE(picker->selected()[0].shape.IsSame(hovered->shape));

    const qsizetype before = selections.count();
    sendMouse(*canvas, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton);
    sendMouse(*canvas, QEvent::MouseMove, middle + QPointF(30.0, 0.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(*canvas, QEvent::MouseButtonRelease, middle + QPointF(30.0, 0.0), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    EXPECT_EQ(selections.count(), before) << "a drag is no click";

    click(*canvas, middle);
    EXPECT_TRUE(picker->selectHovered());
    const std::vector<app::Pick> taken = picker->takePicks();
    QApplication::processEvents();
    EXPECT_EQ(taken.size(), 1U);
    EXPECT_TRUE(picker->selected().empty());
    EXPECT_TRUE(canvas->chunks().highlight().selected.empty());
    EXPECT_EQ(selections.count(), before + 1) << "taking the picks clears them without a signal";

    click(*canvas, middle);
    ASSERT_EQ(picker->selected().size(), 1U);
    click(*canvas, QPointF(2.0, 2.0));
    EXPECT_TRUE(picker->selected().empty()) << "a click on empty space clears";

    click(*canvas, middle);
    ui->trigger("Fillet");
    QApplication::processEvents();
    EXPECT_EQ(picker->kinds(), (render::PickKinds{.faces = false, .edges = true, .points = false}));
    EXPECT_TRUE(picker->selected().empty()) << "a mode change clears the selection";
    sendMouse(*canvas, QEvent::MouseMove, middle, Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    EXPECT_FALSE(picker->hovered()) << "fillet picks edges only";
    ui->key(Qt::Key_Escape);
    QApplication::processEvents();
    EXPECT_EQ(picker->kinds(), (render::PickKinds{.faces = true, .edges = true, .points = false}));

    click(*canvas, middle);
    ASSERT_EQ(picker->selected().size(), 1U);
    auto* session = ui->window().findChild<app::DesignSession*>();
    ASSERT_NE(session, nullptr);
    session->setBodyVisible(session->model().bodies.at(0).key(), false);
    ui->settle();
    EXPECT_TRUE(picker->selected().empty()) << "hiding the body drops its picks";
    EXPECT_FALSE(picker->hovered());
    session->setBodyVisible(session->model().bodies.at(0).key(), true);
    ui->settle();
}

void boxDrag(render::RhiView& canvas, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers,
             const std::function<void()>& during = {})
{
    const auto send = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, pos, canvas.mapToGlobal(pos), button, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    constexpr int kSteps = 4;
    for (int step = 1; step <= kSteps; ++step) {
        send(QEvent::MouseMove, from + ((to - from) * step / kSteps), Qt::NoButton, Qt::LeftButton);
    }
    if (during) {
        during();
    }
    send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
}

std::vector<render::PickedItem> selectedItems(const render::RhiView& canvas)
{
    const std::set<render::PickedItem>& selected = canvas.chunks().highlight().selected;
    return {selected.begin(), selected.end()};
}

std::size_t countOf(const std::vector<render::PickedItem>& items, render::PickKind kind)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(items, [kind](const render::PickedItem& item) { return item.kind == kind; }));
}

TEST_F(NewViewTest, BoxSelectsWithTheOldViewsRule)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    ui->window().openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" / "dodecahedron.k137");
    ui->settle();
    waitForCamera(*ui->window().cameraCommands());
    auto* band = canvas->findChild<app::BandOverlay*>();
    ASSERT_NE(band, nullptr);
    QSignalSpy selections(picker, &app::PickSource::selectionChanged);
    const QPointF topLeft(2.0, 2.0);
    const QPointF topRight(canvas->width() - 2.0, 2.0);
    const QPointF bottomRight(canvas->width() - 2.0, canvas->height() - 2.0);
    const QPointF middle(canvas->width() / 2.0, canvas->height() / 2.0);

    bool bandShown = false;
    boxDrag(*canvas, topLeft, bottomRight, Qt::NoModifier, [&] { bandShown = band->isVisible(); });
    EXPECT_TRUE(bandShown) << "the band shows while dragging";
    EXPECT_FALSE(band->isVisible()) << "and goes on release";
    EXPECT_EQ(selections.count(), 1);
    const std::vector<render::PickedItem> all = selectedItems(*canvas);
    EXPECT_EQ(all, picker->boxItems(topLeft, bottomRight));
    EXPECT_EQ(countOf(all, render::PickKind::Face), 6U) << "a window over the view takes the faces in sight";
    EXPECT_GT(countOf(all, render::PickKind::Edge), 0U);
    EXPECT_LT(all.size(), 6U + 30U) << "hidden faces and edges stay out";
    const std::optional<std::vector<render::PickedItem>> ids = picker->boxIdItems(topLeft, bottomRight);
    ASSERT_TRUE(ids);
    EXPECT_EQ(countOf(*ids, render::PickKind::Face), countOf(all, render::PickKind::Face));

    boxDrag(*canvas, topRight, middle, Qt::NoModifier);
    const std::vector<render::PickedItem> crossed = selectedItems(*canvas);
    EXPECT_EQ(crossed, picker->boxItems(topRight, middle));
    EXPECT_FALSE(crossed.empty()) << "a crossing to the middle touches the faces there";
    EXPECT_LT(crossed.size(), all.size());
    EXPECT_TRUE(std::ranges::includes(all, crossed));

    boxDrag(*canvas, topLeft, middle, Qt::ShiftModifier);
    std::vector<render::PickedItem> added = crossed;
    for (const render::PickedItem& item : picker->boxItems(topLeft, middle)) {
        if (!std::ranges::binary_search(crossed, item)) {
            added.push_back(item);
        }
    }
    std::ranges::sort(added);
    EXPECT_EQ(selectedItems(*canvas), added) << "Shift adds to the selection";

    boxDrag(*canvas, middle, bottomRight, Qt::NoModifier);
    EXPECT_EQ(selectedItems(*canvas), added) << "a drag from a face boxes nothing";

    ui->trigger("Fillet");
    QApplication::processEvents();
    std::vector<render::PickedItem> seen;
    std::ranges::copy_if(all, std::back_inserter(seen),
                         [](const render::PickedItem& item) { return item.kind == render::PickKind::Edge; });
    EXPECT_EQ(picker->boxItems(topLeft, bottomRight), seen)
        << "fillet boxes the same edges, though its preview gives the old view a model of its own";
    const qsizetype before = selections.count();
    boxDrag(*canvas, topLeft, bottomRight, Qt::NoModifier);
    EXPECT_EQ(selections.count(), before + 1) << "the fillet panel takes the boxed edges";
    ui->key(Qt::Key_Escape);
    QApplication::processEvents();
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

TEST_F(NewViewTest, AnOpenGlBelowThreePointTwoShowsTheRequirementOnce)
{
    const render::DeviceProbe old{
        .created = true, .texelFetch = true, .openGl = true, .openGlEs = false, .major = 3, .minor = 1};
    const render::StartProblem problem = render::startProblem(old);
    ASSERT_EQ(problem, render::StartProblem::TooOld);
    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen);
    host.resize(kWindowSize);
    QLabel* notice = render::makeFailureNotice(render::GraphicsApi::OpenGL, problem, &host);
    host.show();
    QTest::qWait(kSettleMs);
    const QList<QLabel*> messages = host.findChildren<QLabel*>(QStringLiteral("viewFailure"));
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.front(), notice);
    EXPECT_TRUE(notice->text().contains(QStringLiteral("OpenGL 3.2 or OpenGL ES 3.0"))) << notice->text().toStdString();
    EXPECT_TRUE(notice->text().contains(QStringLiteral("--view new")));
}

} // namespace
} // namespace kalve137::uitest
