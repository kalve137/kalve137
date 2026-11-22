// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/grid.h"
#include "render/grid_overlay.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr GridColors kColors{.background = kCanvas,
                             .minor = {0.3F, 0.3F, 0.3F},
                             .major = {0.5F, 0.5F, 0.5F},
                             .axisX = {0.9F, 0.1F, 0.1F},
                             .axisY = {0.1F, 0.9F, 0.1F},
                             .axisZ = {0.1F, 0.1F, 0.9F}};
constexpr camera::Viewport kViewport{320, 240, 1.0};

GridSettings visibleGrid()
{
    return {.visible = true, .adaptive = true, .spacing = 10.0, .plane = {}, .extent = {}, .colors = kColors};
}

camera::Camera lookingAt(const camera::Vec3& center, double degrees, double elevation, double scale)
{
    const double angle = degrees * std::numbers::pi / 180.0;
    const double lift = elevation * std::numbers::pi / 180.0;
    const double reach = 400.0;
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center.x + (reach * std::cos(lift) * std::cos(angle)),
                                        center.y + (reach * std::cos(lift) * std::sin(angle)),
                                        center.z + (reach * std::sin(lift))},
                                       center));
    EXPECT_TRUE(camera.setUp(std::abs(elevation) > 80.0 ? camera::Vec3{0.0, 1.0, 0.0} : camera::Vec3{0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(scale));
    return camera;
}

bool nearColor(const VertexColor& a, const VertexColor& b)
{
    for (std::size_t c = 0; c < a.size(); ++c) {
        if (std::abs(static_cast<int>(a.at(c)) - static_cast<int>(b.at(c))) > 1) {
            return false;
        }
    }
    return true;
}

double perPixel(camera::Camera camera)
{
    camera.setAspect(kViewport.aspect());
    return camera.worldPerPixel(kViewport);
}

TEST(GridOverlay, LaysOutLikeTheOldViewWithTwentyFourPiecesPerLine)
{
    const camera::Camera camera = lookingAt({3.0, 4.0, 0.0}, -60.0, 90.0, 120.0);
    GridOverlay overlay;
    overlay.setSettings(visibleGrid());
    overlay.update(camera, kViewport, {});
    const double wpp = perPixel(camera);
    const GridLayout& layout = overlay.layout();
    EXPECT_EQ(layout.step, gridStep(true, 10.0, wpp * kGridMinCellPixels));
    EXPECT_NEAR(layout.radius, 0.5 * std::hypot(320.0, 240.0) * wpp * kGridRadiusFactor, 1e-9);
    EXPECT_EQ(layout.center.u, 0.0);
    EXPECT_EQ(layout.center.v, 0.0);
    const GridDraw grid = overlay.draw(camera, kViewport);
    EXPECT_EQ(grid.lines.size(), gridLines(layout).size() * static_cast<std::size_t>(kGridPiecesPerLine));
    EXPECT_EQ(grid.wash.size(), static_cast<std::size_t>(kGridWashSegments) * 3);
    EXPECT_GT(grid.revision, 0U);
}

TEST(GridOverlay, FadesEachLineTowardTheBackgroundAtTheRim)
{
    const camera::Camera camera = lookingAt({}, -60.0, 90.0, 120.0);
    GridOverlay overlay;
    overlay.setSettings(visibleGrid());
    overlay.update(camera, kViewport, {});
    const GridDraw grid = overlay.draw(camera, kViewport);
    const VertexColor background = vertexColor(kCanvas);
    bool sawAxisX = false;
    for (const OverlayLine& line : grid.lines) {
        if (std::hypot(line.start[0], line.start[1]) >= overlay.layout().radius * 0.999) {
            EXPECT_TRUE(nearColor(line.startColor, background)) << "a line starts on the rim in the background color";
        }
        if (std::abs(line.start[1]) < 1e-6 && std::abs(line.end[1]) < 1e-6 && std::abs(line.start[0]) < 1e-3) {
            EXPECT_TRUE(nearColor(line.startColor, vertexColor(kColors.axisX)))
                << "the X axis is unfaded at the center";
            EXPECT_FLOAT_EQ(line.width, static_cast<float>(kGridAxisWidth));
            sawAxisX = true;
        }
    }
    EXPECT_TRUE(sawAxisX);
}

TEST(GridOverlay, DrawsTheAxesOverEveryOtherLine)
{
    const camera::Camera camera = lookingAt({}, -60.0, 90.0, 120.0);
    GridOverlay overlay;
    overlay.setSettings(visibleGrid());
    overlay.update(camera, kViewport, {});
    const GridDraw grid = overlay.draw(camera, kViewport);
    const auto isAxis = [](const OverlayLine& line) { return line.width == static_cast<float>(kGridAxisWidth); };
    const auto firstAxis = std::ranges::find_if(grid.lines, isAxis);
    ASSERT_NE(firstAxis, grid.lines.end());
    EXPECT_TRUE(std::all_of(firstAxis, grid.lines.end(), isAxis));
}

TEST(GridOverlay, RebuildsOnlyWhenTheStaleRuleSaysSo)
{
    camera::Camera camera = lookingAt({}, -60.0, 90.0, 120.0);
    GridOverlay overlay;
    overlay.setSettings(visibleGrid());
    overlay.update(camera, kViewport, {});
    const std::uint64_t first = overlay.draw(camera, kViewport).revision;
    const double radius = overlay.layout().radius;

    ASSERT_TRUE(camera.setEyeAndCenter(camera.eye() + camera::Vec3{radius * 0.02, 0.0, 0.0},
                                       camera.center() + camera::Vec3{radius * 0.02, 0.0, 0.0}));
    overlay.update(camera, kViewport, {});
    EXPECT_EQ(overlay.draw(camera, kViewport).revision, first) << "a small pan keeps the lines";

    ASSERT_TRUE(camera.setEyeAndCenter(camera.eye() + camera::Vec3{radius * 0.5, 0.0, 0.0},
                                       camera.center() + camera::Vec3{radius * 0.5, 0.0, 0.0}));
    overlay.update(camera, kViewport, {});
    EXPECT_NE(overlay.draw(camera, kViewport).revision, first) << "a long pan recenters";

    const std::uint64_t panned = overlay.draw(camera, kViewport).revision;
    ASSERT_TRUE(camera.setScale(camera.scale() / 8.0));
    overlay.update(camera, kViewport, {});
    EXPECT_NE(overlay.draw(camera, kViewport).revision, panned) << "a zoom changes the step";
}

TEST(GridOverlay, WashesOnlyWithoutAnExtentAndHidesWhole)
{
    const camera::Camera camera = lookingAt({}, -60.0, 90.0, 120.0);
    GridOverlay overlay;
    GridSettings settings = visibleGrid();
    settings.extent = GridExtent{{0.0, 0.0, 0.0}, 10.0};
    overlay.setSettings(settings);
    overlay.update(camera, kViewport, {});
    EXPECT_TRUE(overlay.draw(camera, kViewport).wash.empty());
    EXPECT_FALSE(overlay.draw(camera, kViewport).lines.empty());
    EXPECT_NEAR(overlay.layout().radius, 10.0, 1e-12) << "a small extent bounds the disk";

    settings.visible = false;
    overlay.setSettings(settings);
    overlay.update(camera, kViewport, {});
    EXPECT_TRUE(overlay.draw(camera, kViewport).empty());
    EXPECT_FALSE(overlay.bounds().has_value());
}

TEST(GridOverlay, ShiftsOnePixelAwayFromTheEye)
{
    for (const double elevation : {60.0, -60.0}) {
        const camera::Camera camera = lookingAt({}, 30.0, elevation, 120.0);
        GridOverlay overlay;
        overlay.setSettings(visibleGrid());
        overlay.update(camera, kViewport, {});
        const GridDraw grid = overlay.draw(camera, kViewport);
        const double expected = (elevation > 0.0 ? -1.0 : 1.0) * kGridDepthPixels * perPixel(camera);
        EXPECT_NEAR(grid.shift[2], expected, 1e-6) << elevation;
        EXPECT_EQ(grid.shift[0], 0.0F);
        EXPECT_EQ(grid.shift[1], 0.0F);
    }
}

double ndcOf(const camera::Camera& camera, const camera::Vec3& point)
{
    const camera::Vec3 projected = camera.project(point);
    return projected.z;
}

TEST(GridOverlay, TheWideRangeMapsOntoTheBodyRange)
{
    const camera::Box bodies{{-5.0, -5.0, 0.0}, {5.0, 5.0, 10.0}};
    const camera::Box reach{{-900.0, -900.0, -1.0}, {900.0, 900.0, 1.0}};
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        camera::Camera camera = lookingAt({}, 30.0, 8.0, 40.0);
        camera.setProjectionType(projection);
        EXPECT_TRUE(camera.setScale(40.0));
        camera.setAspect(kViewport.aspect());
        camera.fitDepth(bodies);
        Frame frame = makeFrame(camera, kViewport, {}, kCanvas);
        widenFrame(frame, camera, kViewport, {}, unite(bodies, reach));
        camera::Camera wide = camera;
        wide.fitDepth(unite(bodies, reach));
        for (const camera::Vec3& point : {camera::Vec3{0.0, 0.0, 5.0}, camera::Vec3{300.0, -200.0, 0.0},
                                          camera::Vec3{-600.0, 100.0, 0.0}, camera::Vec3{2.0, 3.0, 9.0}}) {
            const double mapped = (frame.wideDepth[0] * ndcOf(wide, point)) + frame.wideDepth[1];
            EXPECT_NEAR(mapped, ndcOf(camera, point), 1e-6 * std::max(1.0, std::abs(mapped)))
                << static_cast<int>(projection) << " at " << point.x << "," << point.y << "," << point.z;
        }
        EXPECT_LT(wide.zNear(), camera.zNear());
        EXPECT_GT(wide.zFar(), camera.zFar());
    }
}

struct Drawn {
    Image plain;
    Image gridded;
};

Drawn drawBoth(const snapshot::Scene& scene, camera::Camera camera, camera::Projection projection)
{
    const double scale = camera.scale();
    camera.setProjectionType(projection);
    EXPECT_TRUE(camera.setScale(scale));
    camera.fitDepth(sceneBounds(scene));
    ViewportStyle style{.background = kCanvas,
                        .body = {0.6F, 0.65F, 0.7F},
                        .bodyEdge = {0.07F, 0.1F, 0.14F},
                        .wire = {0.77F, 0.79F, 0.81F},
                        .visualStyle = VisualStyle::ShadedWithEdges,
                        .pixelRatio = 1.0,
                        .cullClosed = true};
    ChunkSet chunks;
    chunks.update(scene, style);
    GridOverlay overlay;
    overlay.setSettings(visibleGrid());
    overlay.update(camera, kViewport, scene.origin);
    Frame frame = makeFrame(camera, kViewport, scene.origin, kCanvas);
    widenFrame(frame, camera, kViewport, scene.origin, unite(sceneBounds(scene), overlay.bounds()));
    CpuBackend backend(4);
    Drawn drawn;
    DrawList list = chunks.draw(frame);
    backend.draw(list, 1, drawn.plain);
    list.grid = overlay.draw(camera, kViewport);
    backend.draw(list, 1, drawn.gridded);
    return drawn;
}

struct Changes {
    std::size_t onBackground = 0;
    std::size_t onBodies = 0;
    std::size_t onSilhouettes = 0;
};

Changes changesOf(const Drawn& drawn, std::uint32_t background)
{
    const Image& plain = drawn.plain;
    constexpr int kRim = 2;
    const auto bordersBackground = [&](int x, int y) {
        for (int ny = std::max(0, y - kRim); ny <= std::min(plain.height - 1, y + kRim); ++ny) {
            for (int nx = std::max(0, x - kRim); nx <= std::min(plain.width - 1, x + kRim); ++nx) {
                if (plain.at(nx, ny) == background) {
                    return true;
                }
            }
        }
        return false;
    };
    Changes changes;
    for (int y = 0; y < plain.height; ++y) {
        for (int x = 0; x < plain.width; ++x) {
            if (plain.at(x, y) == drawn.gridded.at(x, y)) {
                continue;
            }
            if (plain.at(x, y) == background) {
                ++changes.onBackground;
            } else {
                ++(bordersBackground(x, y) ? changes.onSilhouettes : changes.onBodies);
            }
        }
    }
    return changes;
}

std::uint32_t backgroundPixel()
{
    const VertexColor color = vertexColor(kCanvas);
    return 0xFF000000U | (std::uint32_t{color[0]} << 16) | (std::uint32_t{color[1]} << 8) | color[2];
}

void expectGridOnlyAround(const snapshot::Scene& scene, const camera::Vec3& center,
                          std::initializer_list<double> elevations, const std::string& what)
{
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        for (const double elevation : elevations) {
            for (const double degrees : {-35.0, 10.0, 125.0}) {
                const Drawn drawn = drawBoth(scene, lookingAt(center, degrees, elevation, 90.0), projection);
                const Changes changes = changesOf(drawn, backgroundPixel());
                const std::string where = what + " " + std::to_string(static_cast<int>(projection)) + " at " +
                                          std::to_string(elevation) + " and " + std::to_string(degrees);
                EXPECT_EQ(changes.onBodies, 0U) << where << ": the grid never shows through the body";
                EXPECT_LE(changes.onSilhouettes, 8U) << where << ": only the face bias at grazing rims lets it touch";
                EXPECT_GT(changes.onBackground, 500U) << where << ": the grid shows around the body";
            }
        }
    }
}

TEST(GridOverlay, AFaceInTheGridPlaneHidesTheGridFromBothSides)
{
    snapshot::Mesh mesh = *test::grid({0.0, 0.0, 0.0}, 40.0, 6);
    const std::vector<snapshot::Point> rim{
        {-20.0, -20.0, 0.0}, {20.0, -20.0, 0.0}, {20.0, 20.0, 0.0}, {-20.0, 20.0, 0.0}, {-20.0, -20.0, 0.0}};
    mesh.edges.push_back({.id = mesh.edgeCount++, .points = rim});
    const auto plate = test::shared(test::bodyOf(std::make_shared<const snapshot::Mesh>(std::move(mesh))));
    expectGridOnlyAround({.bodies = {plate}, .origin = {}, .hidden = {}}, {}, {60.0, 20.0, 4.0, -4.0, -20.0, -60.0},
                         "plate");
}

TEST(GridOverlay, ABodyStandingOnTheGridHidesTheGridFromAbove)
{
    const auto box = test::shared(test::bodyOf(test::edgedBox({-10.0, -10.0, 0.0}, {10.0, 10.0, 20.0})));
    expectGridOnlyAround({.bodies = {box}, .origin = {}, .hidden = {}}, {0.0, 0.0, 5.0}, {60.0, 20.0, 4.0}, "box");
}

TEST(GridOverlay, AGridOutsideTheBodyRangeDrawsInFrontOrBehind)
{
    const auto box = test::shared(test::bodyOf(test::edgedBox({-10.0, -10.0, 50.0}, {10.0, 10.0, 60.0})));
    const snapshot::Scene scene{.bodies = {box}, .origin = {}, .hidden = {}};
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        const Drawn above = drawBoth(scene, lookingAt({0.0, 0.0, 55.0}, 20.0, 50.0, 60.0), projection);
        const Changes behind = changesOf(above, backgroundPixel());
        EXPECT_EQ(behind.onBodies + behind.onSilhouettes, 0U) << "the grid beyond the far end stays behind the body";
        EXPECT_GT(behind.onBackground, 500U) << "and still draws whole";

        const Drawn below = drawBoth(scene, lookingAt({0.0, 0.0, 55.0}, 20.0, -50.0, 60.0), projection);
        const Changes front = changesOf(below, backgroundPixel());
        EXPECT_GT(front.onBodies, 200U) << "the grid before the near end draws over the body";
    }
}

TEST(GridOverlay, ALowOrbitRunsTheGridToTheHorizonWithoutCuts)
{
    const auto box = test::shared(test::bodyOf(test::edgedBox({-2.5, -2.5, 0.0}, {2.5, 2.5, 5.0})));
    const snapshot::Scene scene{.bodies = {box}, .origin = {}, .hidden = {}};
    const Drawn drawn = drawBoth(scene, lookingAt({0.0, 0.0, 2.5}, 30.0, 3.0, 20.0), camera::Projection::Perspective);
    const Changes changes = changesOf(drawn, backgroundPixel());
    EXPECT_EQ(changes.onBodies, 0U) << "the back edges stay hidden and the faces whole";
    std::size_t rowsWithGrid = 0;
    for (int y = 0; y < drawn.gridded.height; ++y) {
        for (int x = 0; x < drawn.gridded.width; ++x) {
            if (drawn.gridded.at(x, y) != drawn.plain.at(x, y)) {
                ++rowsWithGrid;
                break;
            }
        }
    }
    EXPECT_GT(rowsWithGrid, static_cast<std::size_t>(kViewport.height / 3)) << "the grid reaches toward the horizon";
}

} // namespace
} // namespace kalve137::render
