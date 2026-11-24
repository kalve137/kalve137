// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewport_ui_support.h"

#include "../render/test_meshes.h"
#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "app/view/band_overlay.h"
#include "app/view/camera_commands.h"
#include "app/view/pick.h"
#include "app/view/viewport_navigation.h"
#include "app/view/viewport_picker.h"
#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/datums.h"
#include "render/draw_list.h"
#include "render/graphics_api.h"
#include "render/pick_id.h"
#include "render/rhi/rhi_backend.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "snapshot/sketch.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>

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
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

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

render::Frame frameOf(const render::Viewport& canvas)
{
    return render::makeFrame(canvas.camera(), canvas.viewport(), canvas.scene().origin,
                             canvas.viewportStyle().background);
}

QPointF devicePixelOf(const render::Viewport& canvas, const snapshot::Point& point)
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

QPointF logicalPixelOf(const render::Viewport& canvas, const snapshot::Point& point)
{
    return devicePixelOf(canvas, point) / canvas.viewport().pixelRatio;
}

void expectMarkersFollowTheCamera(render::Viewport& canvas, const std::string& api)
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

TEST_F(ViewportTest, DrawsMarkersOnTheirAnchorsAcrossCameraMovesOnEveryApi)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    expectMarkersFollowTheCamera(*canvas, "main");
    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::Viewport view(api);
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

void expectCpuIds(render::Viewport& canvas, const std::string& api)
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

TEST_F(ViewportTest, TheIdPassMatchesTheCpuIds)
{
    render::Viewport* canvas = ui->viewport();
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
        render::Viewport view(api);
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

TEST_F(ViewportTest, PicksOnDemandAndOnlyWhenStale)
{
    render::Viewport* canvas = ui->viewport();
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

TEST_F(ViewportTest, SketchMarksKeepTheIdPassAndSketchEditsRedrawIt)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    canvas->setScene({.bodies = {}, .origin = {}, .hidden = {}});
    canvas->setCamera(obliqueAt({}, 60.0));
    canvas->setSketchLook({.free = {.red = 0.2F, .green = 0.2F, .blue = 0.2F},
                           .construction = {},
                           .profile = {},
                           .highlight = {.red = 0.0F, .green = 0.5F, .blue = 1.0F},
                           .hover = {.red = 1.0F, .green = 0.5F, .blue = 0.0F},
                           .editing = 0,
                           .lines = true,
                           .points = true,
                           .profiles = false,
                           .highlighted = {}});
    const auto sketchAt = [](double y) {
        snapshot::Sketch sketch;
        sketch.feature = 9;
        sketch.curves.push_back({.id = 2,
                                 .kind = snapshot::SketchCurveKind::Line,
                                 .construction = false,
                                 .points = {{-20.0, y, 0.0}, {20.0, y, 0.0}}});
        sketch.points.push_back({.id = 5, .point = {0.0, 15.0, 0.0}, .flags = 0});
        return sketch;
    };
    canvas->setSketches({sketchAt(0.0)});
    ASSERT_FALSE(grabbed(*canvas).isNull());
    const render::PickKinds lines{.faces = false, .edges = false, .sketchLines = true};
    const render::PickKinds points{.faces = false, .edges = false, .sketchPoints = true};
    const render::SketchKey line{.item = render::SketchItem::Line, .feature = 9, .index = 2};
    const render::SketchKey point{.item = render::SketchItem::Point, .feature = 9, .index = 5};
    const auto picked = [&](snapshot::Point at, const render::PickKinds& kinds) -> std::optional<render::SketchKey> {
        const std::optional<render::IdPick> pick = canvas->pickAt(
            logicalPixelOf(*canvas, at), kinds, {.sketchLines = kinds.sketchLines, .sketchPoints = kinds.sketchPoints});
        return pick ? pick->sketch : std::nullopt;
    };

    EXPECT_EQ(picked({0.0, 15.0, 0.0}, points), point);
    const std::uint64_t pointPasses = canvas->idPasses();
    canvas->setSketchMarks({.hovered = point, .selected = {}});
    EXPECT_EQ(picked({0.0, 15.0, 0.0}, points), point) << "a hovered point recolors without moving";
    EXPECT_EQ(canvas->idPasses(), pointPasses);

    EXPECT_EQ(picked({8.0, 0.0, 0.0}, lines), line);
    const std::uint64_t passes = canvas->idPasses();

    canvas->setSketchMarks({.hovered = line, .selected = {}});
    EXPECT_EQ(picked({8.0, 0.0, 0.0}, lines), line) << "picks right after a hover";
    EXPECT_EQ(canvas->idPasses(), passes) << "a hover keeps the id pass";

    canvas->setSketchMarks({.hovered = std::nullopt, .selected = {line}});
    EXPECT_EQ(picked({8.0, 0.0, 0.0}, lines), line) << "picks right after a selection";
    EXPECT_EQ(canvas->idPasses(), passes) << "a selection keeps the id pass";

    canvas->setSketches({sketchAt(-8.0)});
    EXPECT_FALSE(picked({8.0, 0.0, 0.0}, lines)) << "an edit drops the old line from the id pass";
    EXPECT_EQ(canvas->idPasses(), passes + 1) << "an edit makes the id pass stale";
    EXPECT_EQ(picked({8.0, -8.0, 0.0}, lines), line) << "picks right after an edit";
}

void expectDatumPicksFollowTheCamera(render::Viewport& canvas, const std::string& api)
{
    constexpr double kIntoPlane = 25.0;
    const render::PickKinds kinds{.faces = false, .edges = false, .planes = true, .axes = true};
    const render::IdStyle style{.planes = true, .axes = true};
    const GridHidden gridOff(canvas);
    const snapshot::Color color{0.9F, 0.6F, 0.4F};
    const render::Datums datums{
        .originPlanes = {{.id = 0, .anchor = {60.0, 0.0, 0.0}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}}},
        .originAxes = {{.id = 0, .anchor = {60.0, -40.0, 0.0}, .direction = {0.0, 0.0, 1.0}, .color = color}},
        .planes = {{.id = 1, .anchor = {}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}}},
        .axes = {{.id = 2, .anchor = {0.0, -40.0, 0.0}, .direction = {0.0, 0.0, 1.0}, .color = color}},
        .planeColor = color};
    canvas.setScene({.bodies = {}, .origin = {}, .hidden = {}});
    canvas.setDatums(datums);
    const std::array frames{obliqueAt({30.0, -15.0, 0.0}, 140.0), obliqueAt({30.0, 10.0, 0.0}, 180.0),
                            obliqueAt({30.0, -35.0, 10.0}, 140.0)};
    const std::array planes{
        std::pair{snapshot::Point{}, render::DatumKey{.kind = render::DatumKind::Plane, .id = 1}},
        std::pair{snapshot::Point{60.0, 0.0, 0.0}, render::DatumKey{.kind = render::DatumKind::OriginPlane, .id = 0}}};
    const std::array axes{
        std::pair{snapshot::Point{0.0, -40.0, 10.0}, render::DatumKey{.kind = render::DatumKind::Axis, .id = 2}},
        std::pair{snapshot::Point{60.0, -40.0, 10.0},
                  render::DatumKey{.kind = render::DatumKind::OriginAxis, .id = 0}}};
    ASSERT_FALSE(grabbed(canvas).isNull());
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        canvas.setCamera(frames.at(frame));
        for (const auto& [anchor, key] : planes) {
            const QPointF corner = logicalPixelOf(canvas, {anchor[0] + 1.0, anchor[1] + 1.0, anchor[2]});
            const QPointF at = toward(logicalPixelOf(canvas, anchor), corner, kIntoPlane);
            const std::optional<render::IdPick> pick = canvas.pickAt(at, kinds, style);
            ASSERT_TRUE(pick.has_value()) << api << " frame " << frame << " plane " << key.id;
            EXPECT_EQ(pick->datum, key) << api << " frame " << frame;
        }
        for (const auto& [point, key] : axes) {
            const std::optional<render::IdPick> pick = canvas.pickAt(logicalPixelOf(canvas, point), kinds, style);
            ASSERT_TRUE(pick.has_value()) << api << " frame " << frame << " axis " << key.id;
            EXPECT_EQ(pick->datum, key) << api << " frame " << frame;
        }
    }
    canvas.setDatums({});
}

TEST_F(ViewportTest, PicksPlanesAndAxesAfterTheCameraMovesOnEveryApi)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    expectDatumPicksFollowTheCamera(*canvas, "main");
    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::Viewport view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        expectDatumPicksFollowTheCamera(view, std::string(render::graphicsApiName(api)));
    }
}

TEST_F(ViewportTest, ABodyBroughtNearerWithoutACameraMoveDrawsWholeAndPicks)
{
    render::Viewport* canvas = ui->viewport();
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

void click(render::Viewport& canvas, QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, canvas.mapToGlobal(position), Qt::LeftButton, Qt::LeftButton,
                      modifiers);
    QApplication::sendEvent(&canvas, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, canvas.mapToGlobal(position), Qt::LeftButton,
                        Qt::NoButton, modifiers);
    QApplication::sendEvent(&canvas, &release);
    QApplication::processEvents();
}

TEST_F(ViewportTest, HoverAndClicksSelectThroughTheIdPass)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
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

void boxDrag(render::Viewport& canvas, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers,
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

std::vector<render::PickedItem> selectedItems(const render::Viewport& canvas)
{
    const std::set<render::PickedItem>& selected = canvas.chunks().highlight().selected;
    return {selected.begin(), selected.end()};
}

std::size_t countOf(const std::vector<render::PickedItem>& items, render::PickKind kind)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(items, [kind](const render::PickedItem& item) { return item.kind == kind; }));
}

TEST_F(ViewportTest, BoxSelectsWithTheOldViewsRule)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
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

} // namespace
} // namespace kalve137::uitest
