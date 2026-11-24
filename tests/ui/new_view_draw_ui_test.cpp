// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "new_view_ui_support.h"

#include "../render/test_meshes.h"
#include "app/shell/main_window.h"
#include "app/view/camera_commands.h"
#include "app/view/new_view_navigation.h"
#include "app/view/new_view_picker.h"
#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/datums.h"
#include "render/graphics_api.h"
#include "render/pick_id.h"
#include "render/rhi/rhi_view.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

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

} // namespace
} // namespace kalve137::uitest
