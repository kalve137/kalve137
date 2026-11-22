// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "../render/test_meshes.h"
#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "app/view/camera_commands.h"
#include "app/view/new_view_navigation.h"
#include "app/view/new_view_picker.h"
#include "app/view/pick.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "geometry/exchange.h"
#include "geometry/shapes.h"
#include "pick/scene_rays.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/graphics_api.h"
#include "render/pick_id.h"
#include "render/rhi/rhi_backend.h"
#include "render/rhi/rhi_view.h"
#include "render/style.h"
#include "snapshot/body.h"
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
    const render::DrawList list = chunks.draw(render::makeFrame(
        canvas.camera(), canvas.viewport(), canvas.scene().origin, canvas.viewportStyle().background));
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
    const app::SnapshotNavigationScene scene(*canvas, session->rays());
    const camera::Viewport viewport = *commands.viewport();
    const std::optional<camera::Vec3> hit =
        scene.pick(commands.camera(), viewport, {viewport.width / 2, viewport.height / 2}, true);
    ASSERT_TRUE(hit) << "the ray through the middle hits the part";
    EXPECT_LT(camera::dot(*hit - commands.camera().center(), commands.camera().direction()), 0.0);
    EXPECT_FALSE(scene.pick(commands.camera(), viewport, {2, 2}, true)) << "the corner ray misses";
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
    EXPECT_EQ(session->rays().builds(), builds) << "the wheel and the orbit share the scene's one ray scene";

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

TEST_F(NewViewTest, DrawsHighlightsLikeTheCpuBackend)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({990.0, -1010.0, -10.0}, {1010.0, -990.0, 10.0})));
    canvas->setScene({.bodies = {box}, .origin = origin, .hidden = {}});
    canvas->setCamera(obliqueAt({1000.0, -1000.0, 0.0}, 50.0));
    const auto item = [](render::PickKind kind, std::uint32_t index) {
        return render::PickedItem{.feature = 1, .part = 0, .kind = kind, .index = index};
    };
    const render::Highlight highlight{
        .hovered = {item(render::PickKind::Face, 5)},
        .selected = {item(render::PickKind::Face, 2)},
    };
    const render::ViewportStyle original = canvas->viewportStyle();
    for (const render::VisualStyle visual : {render::VisualStyle::ShadedWithEdges, render::VisualStyle::Shaded}) {
        render::ViewportStyle style = original;
        style.visualStyle = visual;
        canvas->setViewportStyle(style);
        canvas->setHighlight({});
        const QImage plain = grabbed(*canvas);
        render::Highlight withEdges = highlight;
        for (std::uint32_t edge = 0; edge < box->mesh->edgeCount; ++edge) {
            (edge % 2 == 0 ? withEdges.hovered : withEdges.selected).insert(item(render::PickKind::Edge, edge));
        }
        canvas->setHighlight(withEdges);
        const QImage gpu = grabbed(*canvas);
        const render::Image cpu = cpuOf(*canvas);
        const Comparison comparison = compare(gpu, cpu);
        ASSERT_TRUE(comparison.sameSize);
        const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
        EXPECT_GT(differing(plain, gpu), comparison.body / 3) << static_cast<int>(visual) << ": the highlight shows";
        EXPECT_LT(comparison.different, pixels / 200)
            << static_cast<int>(visual) << ": " << comparison.different << " of " << comparison.body
            << " drawn pixels differ by more than " << kBackendTolerance;
        ui->save("highlight " + std::to_string(static_cast<int>(visual)));
    }
    canvas->setHighlight({});
    canvas->setViewportStyle(original);
}

TEST_F(NewViewTest, AChangeShowsInTheNextFrame)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
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
