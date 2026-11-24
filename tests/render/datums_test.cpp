// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/datums.h"
#include "render/draw_list.h"
#include "render/pick_id.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr snapshot::Color kPlane{0.4F, 0.6F, 0.9F};
constexpr camera::Viewport kViewport{480, 360, 1.5};
constexpr double kPixelTolerance = 0.5;
constexpr int kAxisRows = 3;
constexpr double kPatternBits = 16.0;

camera::Camera facing(const camera::Vec3& anchor, const camera::Vec3& toward, const camera::Vec3& up,
                      camera::Projection projection, double scale)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter(anchor + (camera::normalized(toward) * 400.0), anchor));
    EXPECT_TRUE(camera.setUp(up));
    camera.setProjectionType(projection);
    EXPECT_TRUE(camera.setScale(scale));
    camera.setAspect(kViewport.aspect());
    return camera;
}

double screenLength(const camera::Camera& camera, const DatumLine& line)
{
    const auto world = [](const Position& p) {
        return camera::Vec3{static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2])};
    };
    const camera::ScreenPoint a = camera.project(world(line.start), kViewport);
    const camera::ScreenPoint b = camera.project(world(line.end), kViewport);
    return std::hypot(b.x - a.x, b.y - a.y);
}

struct Sized {
    std::string name;
    Datums datums;
    camera::Vec3 anchor;
    camera::Vec3 toward;
    camera::Vec3 up;
    std::function<const DatumLine&(const DatumGeometry&)> line;
    double pixels = 0.0;
};

std::vector<Sized> sizedItems()
{
    const camera::Vec3 tilted = camera::normalized({1.0, 2.0, 2.0});
    const camera::Vec3 across = camera::normalized(camera::cross(tilted, {0.0, 0.0, 1.0}));
    const camera::Vec3 third = camera::cross(tilted, across);
    const camera::Vec3 anchor{30.0, -20.0, 10.0};
    return {
        {.name = "origin plane",
         .datums = {.originPlanes = {{.id = 0, .anchor = {}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}}},
                    .originAxes = {},
                    .planes = {},
                    .axes = {},
                    .planeColor = kPlane},
         .anchor = {},
         .toward = {0.0, 0.0, 1.0},
         .up = {0.0, 1.0, 0.0},
         .line = [](const DatumGeometry& g) -> const DatumLine& { return g.originLines.at(0); },
         .pixels = kOriginPlanePixels},
        {.name = "origin axis",
         .datums = {.originPlanes = {},
                    .originAxes = {{.id = 0, .anchor = {}, .direction = {1.0, 0.0, 0.0}, .color = kPlane}},
                    .planes = {},
                    .axes = {},
                    .planeColor = kPlane},
         .anchor = {},
         .toward = {0.0, 0.0, 1.0},
         .up = {0.0, 1.0, 0.0},
         .line = [](const DatumGeometry& g) -> const DatumLine& { return g.originLines.at(0); },
         .pixels = kOriginAxisPixels},
        {.name = "construction plane",
         .datums = {.originPlanes = {},
                    .originAxes = {},
                    .planes = {{.id = 7, .anchor = anchor, .u = across, .v = third}},
                    .axes = {},
                    .planeColor = kPlane},
         .anchor = anchor,
         .toward = tilted,
         .up = third,
         .line = [](const DatumGeometry& g) -> const DatumLine& { return g.lines.at(0); },
         .pixels = kConstructionPlanePixels},
        {.name = "construction axis",
         .datums = {.originPlanes = {},
                    .originAxes = {},
                    .planes = {},
                    .axes = {{.id = 8, .anchor = anchor, .direction = across, .color = kPlane}},
                    .planeColor = kPlane},
         .anchor = anchor,
         .toward = tilted,
         .up = third,
         .line = [](const DatumGeometry& g) -> const DatumLine& { return g.lines.at(0); },
         .pixels = kConstructionAxisPixels},
    };
}

TEST(Datums, EachItemKeepsItsPixelSizeAtTwoZoomsInBothProjections)
{
    for (const Sized& item : sizedItems()) {
        for (const camera::Projection projection :
             {camera::Projection::Orthographic, camera::Projection::Perspective}) {
            for (const double scale : {60.0, 240.0}) {
                const camera::Camera camera = facing(item.anchor, item.toward, item.up, projection, scale);
                const DatumGeometry geometry = buildDatums(item.datums, camera, kViewport, {});
                EXPECT_NEAR(screenLength(camera, item.line(geometry)), item.pixels, kPixelTolerance)
                    << item.name << " in " << static_cast<int>(projection) << " at scale " << scale;
            }
        }
    }
}

TEST(Datums, AnItemFurtherAwayKeepsItsSizeAtItsOwnDepth)
{
    const DatumPlane near{.id = 1, .anchor = {0.0, 0.0, 100.0}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}};
    const DatumPlane far{.id = 2, .anchor = {0.0, 0.0, -100.0}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}};
    const Datums datums{.originPlanes = {}, .originAxes = {}, .planes = {near, far}, .axes = {}, .planeColor = kPlane};
    const camera::Camera camera = facing({}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, camera::Projection::Perspective, 120.0);
    const DatumGeometry geometry = buildDatums(datums, camera, kViewport, {});
    ASSERT_EQ(geometry.lines.size(), 8U);
    for (std::size_t outline = 0; outline < 2; ++outline) {
        EXPECT_NEAR(screenLength(camera, geometry.lines.at(outline * 4)), kConstructionPlanePixels, kPixelTolerance)
            << "outline " << outline;
    }
    ASSERT_EQ(geometry.fills.size(), 12U);
    EXPECT_FLOAT_EQ(geometry.fills.front().position[2], -100.0F) << "the far plane fills first";
}

TEST(Datums, FitTakesTheAnchorsAndGrowsASmallSpanToTheEmptyView)
{
    const camera::Box bodies{{-5.0, -5.0, 0.0}, {5.0, 5.0, 10.0}};
    const std::array anchors{camera::Vec3{200.0, 0.0, 0.0}, camera::Vec3{0.0, 0.0, 0.0}};
    const std::optional<camera::Box> both = fitBox(bodies, anchors, 60.0);
    ASSERT_TRUE(both);
    EXPECT_DOUBLE_EQ(both->max.x, 200.0);
    EXPECT_DOUBLE_EQ(both->min.y, -5.0);

    const std::array small{camera::Vec3{10.0, 20.0, 0.0}, camera::Vec3{30.0, 20.0, 0.0}};
    const std::optional<camera::Box> grown = fitBox(std::nullopt, small, 60.0);
    ASSERT_TRUE(grown);
    EXPECT_DOUBLE_EQ(grown->min.x, -40.0);
    EXPECT_DOUBLE_EQ(grown->max.x, 80.0);
    EXPECT_DOUBLE_EQ(grown->min.z, -60.0);

    const std::array wide{camera::Vec3{-300.0, 0.0, 0.0}, camera::Vec3{300.0, 0.0, 0.0}};
    const std::optional<camera::Box> kept = fitBox(std::nullopt, wide, 60.0);
    ASSERT_TRUE(kept);
    EXPECT_DOUBLE_EQ(kept->min.x, -300.0);
    EXPECT_DOUBLE_EQ(kept->max.y, 60.0);

    EXPECT_FALSE(fitBox(std::nullopt, {}, 60.0));
    const std::optional<camera::Box> alone = fitBox(bodies, {}, 60.0);
    ASSERT_TRUE(alone);
    EXPECT_DOUBLE_EQ(alone->max.z, 10.0);
}

std::uint32_t pixelOf(const snapshot::Color& color)
{
    const VertexColor bytes = vertexColor(color);
    return 0xFF000000U | (std::uint32_t{bytes[0]} << 16) | (std::uint32_t{bytes[1]} << 8) | bytes[2];
}

struct Drawn {
    Image plain;
    Image marked;
};

Drawn draw(const snapshot::Scene& scene, const camera::Camera& view, const Datums& datums)
{
    camera::Camera camera = view;
    camera.fitDepth(sceneBounds(scene));
    const ViewportStyle style{.background = kCanvas,
                              .body = {0.6F, 0.65F, 0.7F},
                              .bodyEdge = {0.07F, 0.1F, 0.14F},
                              .wire = {0.77F, 0.79F, 0.81F},
                              .visualStyle = VisualStyle::Shaded,
                              .pixelRatio = kViewport.pixelRatio,
                              .cullClosed = true};
    ChunkSet chunks;
    chunks.update(scene, style);
    const DatumGeometry geometry = buildDatums(datums, camera, kViewport, scene.origin);
    Frame frame = makeFrame(camera, kViewport, scene.origin, kCanvas);
    widenFrame(frame, camera, kViewport, scene.origin, unite(sceneBounds(scene), geometry.bounds));
    DrawList list = chunks.draw(frame);
    list.pixelRatio = kViewport.pixelRatio;
    CpuBackend backend(4);
    Drawn drawn;
    backend.draw(list, 1, drawn.plain);
    list.datums = {.revision = 1,
                   .fills = geometry.fills,
                   .lines = geometry.lines,
                   .originFills = geometry.originFills,
                   .originLines = geometry.originLines};
    backend.draw(list, 1, drawn.marked);
    return drawn;
}

std::size_t changedOnBody(const Drawn& drawn)
{
    const std::uint32_t background = pixelOf(kCanvas);
    std::size_t changed = 0;
    for (int y = 0; y < drawn.plain.height; ++y) {
        for (int x = 0; x < drawn.plain.width; ++x) {
            changed += drawn.plain.at(x, y) != background && drawn.plain.at(x, y) != drawn.marked.at(x, y) ? 1 : 0;
        }
    }
    return changed;
}

TEST(Datums, OriginPlanesDrawOverBodiesAndConstructionPlanesSortByDepth)
{
    const auto box = test::shared(test::bodyOf(test::edgedBox({-20.0, -20.0, -20.0}, {20.0, 20.0, 20.0})));
    const snapshot::Scene scene{.bodies = {box}, .origin = {}, .hidden = {}};
    const camera::Camera camera = facing({}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, camera::Projection::Perspective, 120.0);
    const auto plane = [](double z) {
        return Datums{.originPlanes = {},
                      .originAxes = {},
                      .planes = {{.id = 1, .anchor = {0.0, 0.0, z}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}}},
                      .axes = {},
                      .planeColor = kPlane};
    };
    EXPECT_GT(changedOnBody(draw(scene, camera, plane(60.0))), 1000U) << "a plane in front of the body shows";
    EXPECT_EQ(changedOnBody(draw(scene, camera, plane(-60.0))), 0U) << "a plane behind the body stays hidden";
    const Datums origin{.originPlanes = {{.id = 0, .anchor = {}, .u = {1.0, 0.0, 0.0}, .v = {0.0, 1.0, 0.0}}},
                        .originAxes = {},
                        .planes = {},
                        .axes = {},
                        .planeColor = kPlane};
    EXPECT_GT(changedOnBody(draw(scene, camera, origin)), 1000U) << "the origin plane inside the body still shows";
}

TEST(Datums, AxesDashInTheOldViewsPatterns)
{
    const snapshot::Scene scene{.bodies = {}, .origin = {}, .hidden = {}};
    const camera::Camera camera = facing({}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, camera::Projection::Orthographic, 120.0);
    for (const auto& [kind, pattern] :
         {std::pair{"construction", kDashPattern}, std::pair{"origin", kDotDashPattern}}) {
        const bool origin = std::string(kind) == "origin";
        const DatumAxis axis{.id = 1, .anchor = {}, .direction = {1.0, 0.0, 0.0}, .color = kPlane};
        const Datums datums{.originPlanes = {},
                            .originAxes = origin ? std::vector{axis} : std::vector<DatumAxis>{},
                            .planes = {},
                            .axes = origin ? std::vector<DatumAxis>{} : std::vector{axis},
                            .planeColor = kPlane};
        const Drawn drawn = draw(scene, camera, datums);
        const int middle = drawn.marked.height / 2;
        int lit = 0;
        for (int x = 0; x < drawn.marked.width; ++x) {
            bool shown = false;
            for (int y = middle - kAxisRows; y <= middle + kAxisRows; ++y) {
                shown = shown || drawn.marked.at(x, y) != pixelOf(kCanvas);
            }
            lit += shown ? 1 : 0;
        }
        const double device = (origin ? kOriginAxisPixels : kConstructionAxisPixels) * kViewport.pixelRatio;
        const double share = static_cast<double>(std::popcount(static_cast<std::uint16_t>(pattern))) / kPatternBits;
        EXPECT_NEAR(lit, device * share, device / kPatternBits * 2.0) << kind << " draws " << lit << " of " << device;
    }
}

TEST(Datums, PointsBecomeTopmostMarkersAndOnlyConstructionPointsPick)
{
    const camera::Camera camera = facing({}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, camera::Projection::Orthographic, 120.0);
    const snapshot::Color white{0.95F, 0.96F, 0.97F};
    Datums datums{.originPlanes = {},
                  .originAxes = {},
                  .planes = {},
                  .axes = {},
                  .planeColor = kPlane,
                  .points = {{.id = 4, .anchor = {10.0, 0.0, 0.0}}, {.id = 9, .anchor = {0.0, 300.0, 0.0}}},
                  .originPoint = true,
                  .originPointColor = white};
    const DatumGeometry geometry = buildDatums(datums, camera, kViewport, {});
    ASSERT_EQ(geometry.markers.size(), 3U);
    const MarkerItem& origin = geometry.markers[0];
    EXPECT_EQ(origin.shape, MarkerShape::Ball);
    EXPECT_EQ(origin.scale, MarkerScale::Medium);
    EXPECT_EQ(origin.color, white);
    EXPECT_EQ(origin.flags, kMarkerTopmost) << "the origin point never picks";
    for (std::size_t i = 1; i < geometry.markers.size(); ++i) {
        const MarkerItem& point = geometry.markers[i];
        EXPECT_EQ(point.shape, MarkerShape::RingPlus);
        EXPECT_EQ(point.scale, MarkerScale::Medium);
        EXPECT_EQ(point.color, kPlane);
        EXPECT_EQ(point.flags, kMarkerTopmost | kMarkerPickable);
        const PickId id = decodePickId(point.pick);
        EXPECT_EQ(id.chunk, kMarkerChunk);
        EXPECT_EQ(id.kind, PickKind::Point);
        const std::optional<DatumKey> key = datumAt(datums, id.chunk, id.entry);
        ASSERT_TRUE(key.has_value());
        EXPECT_EQ(*key, (DatumKey{.kind = DatumKind::Point, .id = datums.points[i - 1].id}));
        EXPECT_TRUE(contains(datums, *key));
    }
    ASSERT_TRUE(geometry.bounds.has_value());
    EXPECT_DOUBLE_EQ(geometry.bounds->max.y, 300.0) << "points join the depth range";
    EXPECT_EQ(datumAnchors(datums).size(), 3U) << "points and the origin point join Fit";
    EXPECT_FALSE(datumAt(datums, kMarkerChunk, 2).has_value());

    const DatumGeometry hovered = buildDatums(datums, camera, kViewport, {},
                                              {.hovered = DatumKey{.kind = DatumKind::Point, .id = 4}, .selected = {}});
    EXPECT_NE(hovered.markers[1].color, kPlane) << "a hovered point lights up";
    EXPECT_EQ(hovered.markers[2].color, kPlane);

    const Marker placed = placeMarker(hovered.markers[1], {5.0, 0.0, 0.0});
    EXPECT_EQ(placed.position, (Position{5.0F, 0.0F, 0.0F}));
    EXPECT_EQ(shapeOf(placed), MarkerShape::RingPlus);
    EXPECT_EQ(flagsOf(placed), kMarkerTopmost | kMarkerPickable);
    EXPECT_EQ(pickOf(placed), hovered.markers[1].pick);
    EXPECT_FLOAT_EQ(placed.ring, static_cast<float>(kMarkerMediumRing));
    EXPECT_FLOAT_EQ(placed.cross, static_cast<float>(kMarkerMediumCross));

    datums.originPoint = false;
    datums.points.clear();
    EXPECT_TRUE(datums.empty());
    EXPECT_TRUE(buildDatums(datums, camera, kViewport, {}).markers.empty());
}

} // namespace
} // namespace kalve137::render
