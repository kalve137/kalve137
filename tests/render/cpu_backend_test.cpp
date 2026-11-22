// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr snapshot::Color kBody{0.6F, 0.65F, 0.7F};
constexpr ViewportStyle kShaded{
    .background = kCanvas, .body = kBody, .visualStyle = VisualStyle::Shaded, .pixelRatio = 1.0, .cullClosed = true};
constexpr std::uint32_t kTopFace = 5;

camera::Camera lookingDownAt(const snapshot::Point& center, const camera::Vec3& up, double scale)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center[0], center[1], center[2] + 500.0}, {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp(up));
    EXPECT_TRUE(camera.setScale(scale));
    return camera;
}

std::uint32_t backgroundOf(CpuBackend& backend, const Frame& frame)
{
    Image image;
    backend.draw({.frame = frame, .lit = true, .cullClosed = true, .chunks = {}}, 1, image);
    return image.at(0, 0);
}

std::size_t countOf(const Image& image, std::uint32_t pixel)
{
    return static_cast<std::size_t>(std::ranges::count(image.pixels, pixel));
}

std::size_t differences(const Image& a, const Image& b)
{
    std::size_t changed = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        changed += a.pixels[i] != b.pixels[i] ? 1 : 0;
    }
    return changed;
}

int channelOf(std::uint32_t pixel, int shift)
{
    return static_cast<int>((pixel >> shift) & 0xFFU);
}

Image drawnWith(CpuBackend& backend, const snapshot::Scene& scene, const Frame& frame,
                const ViewportStyle& style = kShaded, int supersample = 1)
{
    ChunkSet chunks;
    chunks.update(scene, style);
    Image image;
    backend.draw(chunks.draw(frame), supersample, image);
    return image;
}

camera::Camera obliqueAt(const snapshot::Point& center)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center[0] + 300.0, center[1] - 200.0, center[2] + 250.0},
                                       {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(30.0));
    return camera;
}

TEST(CpuBackend, LeavesNoCracksBetweenFacesOneKilometerOut)
{
    const snapshot::Point center{1000123.4, 999876.5, 1000042.0};
    const snapshot::Point origin{1000000.0, 1000000.0, 1000000.0};
    const auto body = test::shared(test::bodyOf(test::grid(center, 200.0, 40)));
    const snapshot::Scene scene{.bodies = {body}, .origin = origin, .hidden = {}};
    const camera::Camera camera = lookingDownAt(center, camera::normalized({0.3, 1.0, 0.0}), 100.0);
    const Frame frame = makeFrame(camera, {160, 120, 1.0}, origin, kCanvas);
    CpuBackend backend(4);
    const std::uint32_t background = backgroundOf(backend, frame);
    for (const int supersample : {1, 2}) {
        const Image image = drawnWith(backend, scene, frame, kShaded, supersample);
        ASSERT_EQ(image.pixels.size(), 160U * 120U);
        EXPECT_EQ(countOf(image, background), 0U) << "supersample " << supersample;
    }
}

TEST(CpuBackend, AFarBodyMovesTheOriginButNotThePixels)
{
    const snapshot::Point center{1234.5, -567.25, 89.0};
    const auto torus = test::shared(test::bodyOf(test::torus(center)));
    const auto far = test::shared(test::bodyOf(test::box({20000.0, 0.0, 0.0}, {20010.0, 10.0, 10.0}), {}, 1));
    camera::Camera camera = lookingDownAt(center, {0.0, 1.0, 0.0}, 130.0);
    ASSERT_TRUE(
        camera.setEyeAndCenter({center[0], center[1] - 300.0, center[2] + 400.0}, {center[0], center[1], center[2]}));
    const camera::Viewport viewport{200, 160, 1.0};
    CpuBackend backend(4);

    const snapshot::Point near{1000.0, -1000.0, 0.0};
    const Image before = drawnWith(backend, {.bodies = {torus}, .origin = near, .hidden = {}},
                                   makeFrame(camera, viewport, near, kCanvas), kShaded, 2);
    const snapshot::Point moved{11000.0, 0.0, 0.0};
    const Image after = drawnWith(backend, {.bodies = {torus, far}, .origin = moved, .hidden = {}},
                                  makeFrame(camera, viewport, moved, kCanvas), kShaded, 2);

    const std::uint32_t background = backgroundOf(backend, makeFrame(camera, viewport, near, kCanvas));
    EXPECT_GT(before.pixels.size() - countOf(before, background), 4000U) << "the torus fills part of the view";
    EXPECT_LE(differences(before, after), before.pixels.size() / 200)
        << "at most half a percent of the pixels may round differently";
}

TEST(CpuBackend, CullingAClosedBodyChangesNoPixel)
{
    const snapshot::Point center{5.0, 5.0, 5.0};
    const auto box = test::shared(test::bodyOf(test::box({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0})));
    const auto torus = test::shared(test::bodyOf(test::torus({30.0, 0.0, 0.0}), {}, 1));
    const snapshot::Scene scene{.bodies = {box, torus}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(obliqueAt(center), {200, 160, 1.0}, {}, kCanvas);
    CpuBackend backend(4);
    ViewportStyle unculled = kShaded;
    unculled.cullClosed = false;
    const Image culled = drawnWith(backend, scene, frame);
    const Image all = drawnWith(backend, scene, frame, unculled);
    EXPECT_GT(all.pixels.size() - countOf(all, backgroundOf(backend, frame)), 2000U);
    EXPECT_EQ(differences(culled, all), 0U) << "front faces wind counterclockwise, so only hidden faces go";

    snapshot::Mesh inverted = *test::box({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0});
    for (snapshot::Triangle& triangle : inverted.triangles) {
        std::swap(triangle[1], triangle[2]);
    }
    const auto turned = test::shared(test::bodyOf(std::make_shared<const snapshot::Mesh>(std::move(inverted))));
    const snapshot::Scene inside{.bodies = {turned}, .origin = {}, .hidden = {}};
    EXPECT_GT(differences(drawnWith(backend, inside, frame), drawnWith(backend, inside, frame, unculled)), 500U)
        << "culling removes the faces that turn away";
}

TEST(CpuBackend, AnOpenBoxShowsItsInside)
{
    snapshot::Mesh open = *test::box({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0}, false);
    std::vector<snapshot::Triangle> triangles;
    std::vector<std::uint32_t> faces;
    for (std::size_t i = 0; i < open.triangles.size(); ++i) {
        if (open.faces[i] != kTopFace) {
            triangles.push_back(open.triangles[i]);
            faces.push_back(open.faces[i]);
        }
    }
    open.triangles = std::move(triangles);
    open.faces = std::move(faces);
    const auto body = test::shared(test::bodyOf(std::make_shared<const snapshot::Mesh>(std::move(open))));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const camera::Camera camera = lookingDownAt({5.0, 5.0, 5.0}, {0.0, 1.0, 0.0}, 30.0);
    const Frame frame = makeFrame(camera, {120, 120, 1.0}, {}, kCanvas);
    CpuBackend backend(2);
    const Image image = drawnWith(backend, scene, frame);
    EXPECT_NE(image.at(60, 60), backgroundOf(backend, frame)) << "the floor shows through the missing top";
}

TEST(CpuBackend, SkipsHiddenBodies)
{
    const auto box = test::shared(test::bodyOf(test::box({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0})));
    const auto torus = test::shared(test::bodyOf(test::torus({30.0, 0.0, 0.0}), {}, 1));
    const Frame frame = makeFrame(obliqueAt({5.0, 5.0, 5.0}), {200, 160, 1.0}, {}, kCanvas);
    CpuBackend backend(2);
    const Image both = drawnWith(backend, {.bodies = {box, torus}, .origin = {}, .hidden = {}}, frame);
    const Image alone = drawnWith(backend, {.bodies = {box}, .origin = {}, .hidden = {}}, frame);
    const Image hidden = drawnWith(backend, {.bodies = {box}, .origin = {}, .hidden = {torus}}, frame);
    EXPECT_GT(differences(both, alone), 100U);
    EXPECT_EQ(differences(hidden, alone), 0U);
}

TEST(CpuBackend, ShadesWithTheLookFormula)
{
    const snapshot::Point center{0.0, 0.0, 0.0};
    const auto body = test::shared(test::bodyOf(test::torus(center, 192, 96)));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const camera::Camera camera = lookingDownAt(center, {0.0, 1.0, 0.0}, 130.0);
    const camera::Viewport viewport{260, 260, 1.0};
    const Frame frame = makeFrame(camera, viewport, {}, kCanvas);
    CpuBackend backend(4);
    const Image image = drawnWith(backend, scene, frame);

    const camera::ScreenPoint top = camera.project({test::kTorusMajor, 0.0, test::kTorusMinor}, viewport);
    const std::uint32_t pixel = image.at(static_cast<int>(top.x), static_cast<int>(top.y));
    const Linear shaded = lit(frame.eye, frame.eye, linearOf(vertexColor(kBody)), frame);
    EXPECT_NEAR(channelOf(pixel, 16), toSrgb(shaded[0]) * 255.0F, 3.0);
    EXPECT_NEAR(channelOf(pixel, 8), toSrgb(shaded[1]) * 255.0F, 3.0);
    EXPECT_NEAR(channelOf(pixel, 0), toSrgb(shaded[2]) * 255.0F, 3.0);
    const camera::ScreenPoint outer =
        camera.project({-(test::kTorusMajor + test::kTorusMinor) + 2.0, 0.0, 0.0}, viewport);
    const std::uint32_t shadow = image.at(static_cast<int>(outer.x), static_cast<int>(outer.y));
    EXPECT_LT(channelOf(shadow, 16), channelOf(pixel, 16)) << "the side away from the light is darker";
    EXPECT_EQ(image.at(130, 130), backgroundOf(backend, frame)) << "the hole shows the canvas";
}

constexpr snapshot::Color kEdge{1.0F, 0.0F, 1.0F};
constexpr snapshot::Color kWire{0.0F, 1.0F, 1.0F};

ViewportStyle edgedStyle(VisualStyle visual)
{
    ViewportStyle style = kShaded;
    style.bodyEdge = kEdge;
    style.wire = kWire;
    style.visualStyle = visual;
    return style;
}

std::uint32_t pixelOf(const snapshot::Color& color)
{
    const VertexColor encoded = vertexColor(color);
    return 0xFF000000U | (std::uint32_t{encoded[0]} << 16) | (std::uint32_t{encoded[1]} << 8) | encoded[2];
}

camera::Camera orbitingAt(const snapshot::Point& center, double degrees)
{
    const double angle = degrees * std::numbers::pi / 180.0;
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter(
        {center[0] + (360.0 * std::cos(angle)), center[1] + (360.0 * std::sin(angle)), center[2] + 250.0},
        {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(30.0));
    return camera;
}

camera::ScreenPoint screenOf(const Frame& frame, const snapshot::Point& origin, const snapshot::Point& point)
{
    const Position p = relativePosition(point, origin);
    const std::array<float, 16>& m = frame.viewProjection;
    std::array<double, 4> clip{};
    for (std::size_t r = 0; r < clip.size(); ++r) {
        clip.at(r) = (static_cast<double>(m.at(r)) * p[0]) + (static_cast<double>(m.at(r + 4)) * p[1]) +
                     (static_cast<double>(m.at(r + 8)) * p[2]) + static_cast<double>(m.at(r + 12));
    }
    return {(clip[0] / clip[3] + 1.0) * 0.5 * frame.width, (1.0 - (clip[1] / clip[3])) * 0.5 * frame.height};
}

std::uint32_t pixelAt(const Image& image, const Frame& frame, const snapshot::Point& origin,
                      const snapshot::Point& point, double offsetX = 0.0, double offsetY = 0.0)
{
    const camera::ScreenPoint screen = screenOf(frame, origin, point);
    return image.at(static_cast<int>(std::floor(screen.x + offsetX)), static_cast<int>(std::floor(screen.y + offsetY)));
}

bool edgeNear(const Image& image, const Frame& frame, const snapshot::Point& origin, const snapshot::Point& point,
              std::uint32_t edge)
{
    for (const double dy : {-1.0, 0.0, 1.0}) {
        for (const double dx : {-1.0, 0.0, 1.0}) {
            if (pixelAt(image, frame, origin, point, dx, dy) == edge) {
                return true;
            }
        }
    }
    return false;
}

snapshot::Point along(const snapshot::Point& from, const snapshot::Point& to, double t)
{
    return {from[0] + ((to[0] - from[0]) * t), from[1] + ((to[1] - from[1]) * t), from[2] + ((to[2] - from[2]) * t)};
}

const std::vector<std::pair<snapshot::Point, snapshot::Point>> kFrontEdges{{{10.0, 0.0, 0.0}, {10.0, 0.0, 10.0}},
                                                                           {{10.0, 0.0, 10.0}, {10.0, 10.0, 10.0}},
                                                                           {{0.0, 0.0, 10.0}, {10.0, 0.0, 10.0}}};
const std::vector<std::pair<snapshot::Point, snapshot::Point>> kHiddenEdges{
    {{0.0, 10.0, 0.0}, {10.0, 10.0, 0.0}}, {{0.0, 10.0, 0.0}, {0.0, 0.0, 0.0}}, {{0.0, 10.0, 0.0}, {0.0, 10.0, 10.0}}};

TEST(CpuBackend, EachStyleDrawsItsEdgesOverItsFaces)
{
    const auto body = test::shared(test::bodyOf(test::edgedBox({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const camera::Camera camera = orbitingAt({5.0, 5.0, 5.0}, -33.0);
    const camera::Viewport viewport{240, 200, 1.0};
    const Frame frame = makeFrame(camera, viewport, {}, kCanvas);
    CpuBackend backend(4);
    const std::uint32_t background = backgroundOf(backend, frame);
    const Image shaded = drawnWith(backend, scene, frame, edgedStyle(VisualStyle::Shaded));
    const snapshot::Point faceCenter{10.0, 5.0, 5.0};
    const auto at = [&](const Image& image, const snapshot::Point& point) { return pixelAt(image, frame, {}, point); };

    struct Expected {
        VisualStyle visual;
        std::uint32_t edge;
        bool hiddenShow;
    };
    for (const Expected& expected : {Expected{VisualStyle::ShadedWithEdges, pixelOf(kEdge), false},
                                     Expected{VisualStyle::Wireframe, pixelOf(kWire), true},
                                     Expected{VisualStyle::VisibleEdges, pixelOf(kWire), false}}) {
        const Image image = drawnWith(backend, scene, frame, edgedStyle(expected.visual));
        const int style = static_cast<int>(expected.visual);
        for (const auto& [from, to] : kFrontEdges) {
            for (const double t : {0.2, 0.5, 0.8}) {
                EXPECT_TRUE(edgeNear(image, frame, {}, along(from, to, t), expected.edge)) << style;
            }
        }
        for (const auto& [from, to] : kHiddenEdges) {
            EXPECT_EQ(edgeNear(image, frame, {}, along(from, to, 0.3), expected.edge), expected.hiddenShow) << style;
        }
        const std::uint32_t face = at(image, faceCenter);
        switch (expected.visual) {
        case VisualStyle::ShadedWithEdges:
            EXPECT_EQ(face, at(shaded, faceCenter)) << "the bias moves depth, not color";
            break;
        case VisualStyle::Wireframe:
            EXPECT_EQ(face, background);
            break;
        case VisualStyle::VisibleEdges:
            EXPECT_EQ(face, pixelOf(kCanvas));
            break;
        case VisualStyle::Shaded:
            break;
        }
    }
    EXPECT_EQ(countOf(shaded, pixelOf(kEdge)) + countOf(shaded, pixelOf(kWire)), 0U) << "shaded draws no edges";
}

constexpr snapshot::Color kHover{0.2F, 0.8F, 0.3F};
constexpr snapshot::Color kSelected{0.9F, 0.6F, 0.1F};

Image highlighted(CpuBackend& backend, const snapshot::Scene& scene, const Frame& frame, ViewportStyle style,
                  Highlight highlight)
{
    style.hover = kHover;
    style.selected = kSelected;
    ChunkSet chunks;
    chunks.update(scene, style);
    chunks.setHighlight(std::move(highlight));
    Image image;
    backend.draw(chunks.draw(frame), 1, image);
    return image;
}

TEST(CpuBackend, AHighlightBlendsItsLitColorOverTheFace)
{
    const auto body = test::shared(test::bodyOf(test::box({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const camera::Camera camera = lookingDownAt({5.0, 5.0, 5.0}, {0.0, 1.0, 0.0}, 30.0);
    const Frame frame = makeFrame(camera, {120, 120, 1.0}, {}, kCanvas);
    CpuBackend backend(2);
    const PickedItem top{.feature = 1, .part = 0, .kind = PickKind::Face, .index = kTopFace};
    const auto litOf = [&](const snapshot::Color& color) {
        const Linear shaded = lit(frame.eye, frame.eye, linearOf(vertexColor(color)), frame);
        return std::array{toSrgb(shaded[0]) * 255.0F, toSrgb(shaded[1]) * 255.0F, toSrgb(shaded[2]) * 255.0F};
    };
    const auto expectPixel = [&](std::uint32_t pixel, const std::array<float, 3>& expected, const char* what) {
        EXPECT_NEAR(channelOf(pixel, 16), expected[0], 3.0) << what;
        EXPECT_NEAR(channelOf(pixel, 8), expected[1], 3.0) << what;
        EXPECT_NEAR(channelOf(pixel, 0), expected[2], 3.0) << what;
    };
    const std::array<float, 3> base = litOf(kBody);
    const std::array<float, 3> hover = litOf(kHover);
    std::array<float, 3> mixed{};
    for (std::size_t c = 0; c < mixed.size(); ++c) {
        mixed.at(c) = (base.at(c) * (1.0F - kHoverWeight)) + (hover.at(c) * kHoverWeight);
    }
    expectPixel(highlighted(backend, scene, frame, kShaded, {}).at(60, 60), base, "plain");
    expectPixel(highlighted(backend, scene, frame, kShaded, {.hovered = {top}, .selected = {}}).at(60, 60), mixed,
                "hovered");
    expectPixel(highlighted(backend, scene, frame, kShaded, {.hovered = {top}, .selected = {top}}).at(60, 60),
                litOf(kSelected), "selected wins");

    PickedItem side = top;
    side.index = 0;
    const Image other = highlighted(backend, scene, frame, kShaded, {.hovered = {side}, .selected = {}});
    expectPixel(other.at(60, 60), base, "another face's highlight leaves the top alone");
    ViewportStyle flat = kShaded;
    flat.visualStyle = VisualStyle::VisibleEdges;
    const std::uint32_t tinted =
        highlighted(backend, scene, frame, flat, {.hovered = {}, .selected = {top}}).at(60, 60);
    EXPECT_EQ(tinted, pixelOf(kSelected)) << "an unlit style blends the flat colors";
}

TEST(CpuBackend, AHighlightedEdgeTakesItsStateColor)
{
    const auto body = test::shared(test::bodyOf(test::edgedBox({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(orbitingAt({5.0, 5.0, 5.0}, -33.0), {240, 200, 1.0}, {}, kCanvas);
    CpuBackend backend(2);
    std::size_t hoveredPixels = 0;
    std::size_t selectedPixels = 0;
    for (std::uint32_t edge = 0; edge < body->mesh->edgeCount; ++edge) {
        const PickedItem item{.feature = 1, .part = 0, .kind = PickKind::Edge, .index = edge};
        const Image hovered =
            highlighted(backend, scene, frame, edgedStyle(VisualStyle::Shaded), {.hovered = {item}, .selected = {}});
        EXPECT_EQ(countOf(hovered, pixelOf(kEdge)), 0U) << "shaded draws only the highlighted edge";
        hoveredPixels += countOf(hovered, pixelOf(kHover));
        const Image selected = highlighted(backend, scene, frame, edgedStyle(VisualStyle::ShadedWithEdges),
                                           {.hovered = {item}, .selected = {item}});
        EXPECT_EQ(countOf(selected, pixelOf(kHover)), 0U) << "selected wins";
        EXPECT_GT(countOf(selected, pixelOf(kEdge)), 100U) << "the other edges stay";
        selectedPixels += countOf(selected, pixelOf(kSelected));
    }
    EXPECT_GT(hoveredPixels, 200U) << "the shown edges draw in the hover color";
    EXPECT_GT(selectedPixels, 200U);
}

TEST(CpuBackend, EdgesWidenWithThePixelRatio)
{
    const auto body = test::shared(test::bodyOf(test::edgedBox({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const camera::Camera camera = orbitingAt({5.0, 5.0, 5.0}, -33.0);
    CpuBackend backend(2);
    ViewportStyle style = edgedStyle(VisualStyle::Wireframe);
    const Image single = drawnWith(backend, scene, makeFrame(camera, {240, 200, 1.0}, {}, kCanvas), style);
    style.pixelRatio = 2.0;
    const Image doubled = drawnWith(backend, scene, makeFrame(camera, {240, 200, 1.0}, {}, kCanvas), style);
    const double ratio =
        static_cast<double>(countOf(doubled, pixelOf(kWire))) / static_cast<double>(countOf(single, pixelOf(kWire)));
    EXPECT_GT(ratio, 1.6);
    EXPECT_LT(ratio, 2.4);
}

TEST(CpuBackend, AThinWallHidesTheEdgeOnItsFarSide)
{
    constexpr double kThickness = 0.3;
    constexpr double kSide = 40.0;
    const auto body = test::shared(test::bodyOf(test::edgedBox({0.0, 0.0, 0.0}, {kSide, kSide, kThickness})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const double elevation = 25.0 * std::numbers::pi / 180.0;
    const snapshot::Point center{kSide / 2.0, kSide, kThickness / 2.0};
    camera::Camera camera;
    ASSERT_TRUE(camera.setEyeAndCenter(
        {center[0], center[1] - (500.0 * std::cos(elevation)), center[2] + (500.0 * std::sin(elevation))},
        {center[0], center[1], center[2]}));
    ASSERT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    ASSERT_TRUE(camera.setScale(17.0));
    const Frame frame = makeFrame(camera, {400, 300, 1.0}, {}, kCanvas);
    CpuBackend backend(4);
    const Image image = drawnWith(backend, scene, frame, edgedStyle(VisualStyle::ShadedWithEdges));
    std::size_t near = 0;
    std::size_t far = 0;
    constexpr int kSamples = 40;
    for (int i = 1; i < kSamples; ++i) {
        const double x = kSide * (0.25 + (0.5 * i / kSamples));
        near += pixelAt(image, frame, {}, {x, kSide, kThickness}) == pixelOf(kEdge) ? 1 : 0;
        far += pixelAt(image, frame, {}, {x, kSide, 0.0}) == pixelOf(kEdge) ? 1 : 0;
    }
    EXPECT_EQ(near, static_cast<std::size_t>(kSamples - 1)) << "the near edge shows";
    EXPECT_EQ(far, 0U) << "the edge behind the top face stays hidden";
}

TEST(CpuBackend, EdgesStayWholeOnFaceBordersOneKilometerOut)
{
    const snapshot::Point origin{1000000.0, 1000000.0, 1000000.0};
    const snapshot::Point low{1000123.4, 999876.5, 1000042.0};
    const auto body = test::shared(test::bodyOf(test::edgedBox(low, {low[0] + 10.0, low[1] + 10.0, low[2] + 10.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = origin, .hidden = {}};
    const snapshot::Point center{low[0] + 5.0, low[1] + 5.0, low[2] + 5.0};
    const camera::Viewport viewport{240, 200, 1.0};
    CpuBackend backend(4);
    const auto shifted = [&](const snapshot::Point& point) {
        return snapshot::Point{point[0] + low[0], point[1] + low[1], point[2] + low[2]};
    };
    constexpr int kSteps = 40;
    constexpr int kSamples = 24;
    std::size_t missing = 0;
    std::size_t bare = 0;
    for (int step = 0; step < kSteps; ++step) {
        const Frame frame = makeFrame(orbitingAt(center, -33.0 + (0.013 * step)), viewport, origin, kCanvas);
        const Image image = drawnWith(backend, scene, frame, edgedStyle(VisualStyle::ShadedWithEdges));
        const std::uint32_t background = backgroundOf(backend, frame);
        for (const auto& [from, to] : kFrontEdges) {
            const camera::ScreenPoint a = screenOf(frame, origin, shifted(from));
            const camera::ScreenPoint b = screenOf(frame, origin, shifted(to));
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            const double normalX = -(b.y - a.y) / length * 4.0;
            const double normalY = (b.x - a.x) / length * 4.0;
            for (int i = 1; i < kSamples; ++i) {
                const double t = static_cast<double>(i) / kSamples;
                const snapshot::Point point = along(shifted(from), shifted(to), t);
                missing += pixelAt(image, frame, origin, point) == pixelOf(kEdge) ? 0 : 1;
                if (t < 0.2 || t > 0.8) {
                    continue;
                }
                for (const double side : {-1.0, 1.0}) {
                    const std::uint32_t beside = pixelAt(image, frame, origin, point, side * normalX, side * normalY);
                    bare += beside == pixelOf(kEdge) || beside == background ? 1 : 0;
                }
            }
        }
    }
    EXPECT_EQ(missing, 0U) << "every sample on a front edge shows the edge";
    EXPECT_EQ(bare, 0U) << "four pixels off the line a face shows on either side";
}

} // namespace
} // namespace kalve137::render
