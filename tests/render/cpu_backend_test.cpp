// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/scene_cache.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr snapshot::Color kBody{0.6F, 0.65F, 0.7F};
constexpr ViewportStyle kShaded{
    .background = kCanvas, .body = kBody, .visualStyle = VisualStyle::Shaded, .pixelRatio = 1.0};

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
    backend.draw({.frame = frame, .batches = {}}, 1, image);
    return image.at(0, 0);
}

std::size_t countOf(const Image& image, std::uint32_t pixel)
{
    return static_cast<std::size_t>(std::ranges::count(image.pixels, pixel));
}

int channelOf(std::uint32_t pixel, int shift)
{
    return static_cast<int>((pixel >> shift) & 0xFFU);
}

TEST(CpuBackend, LeavesNoCracksBetweenFacesOneKilometerOut)
{
    const snapshot::Point center{1000123.4, 999876.5, 1000042.0};
    const snapshot::Point origin{1000000.0, 1000000.0, 1000000.0};
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(test::grid(center, 200.0, 40)));
    const snapshot::Scene scene{.bodies = {body}, .origin = origin};
    const camera::Camera camera = lookingDownAt(center, camera::normalized({0.3, 1.0, 0.0}), 100.0);
    const Frame frame = makeFrame(camera, {160, 120, 1.0}, origin, kCanvas);
    SceneCache cache;
    const DrawList list = cache.draw(scene, frame, kShaded);
    CpuBackend backend(4);
    const std::uint32_t background = backgroundOf(backend, frame);
    for (const int supersample : {1, 2}) {
        Image image;
        backend.draw(list, supersample, image);
        ASSERT_EQ(image.pixels.size(), 160U * 120U);
        EXPECT_EQ(countOf(image, background), 0U) << "supersample " << supersample;
    }
}

TEST(CpuBackend, AFarBodyMovesTheOriginButNotThePixels)
{
    const snapshot::Point center{1234.5, -567.25, 89.0};
    const auto torus = std::make_shared<const snapshot::Body>(test::bodyOf(test::torus(center)));
    const auto far =
        std::make_shared<const snapshot::Body>(test::bodyOf(test::box({20000.0, 0.0, 0.0}, {20010.0, 10.0, 10.0})));
    camera::Camera camera = lookingDownAt(center, {0.0, 1.0, 0.0}, 130.0);
    ASSERT_TRUE(
        camera.setEyeAndCenter({center[0], center[1] - 300.0, center[2] + 400.0}, {center[0], center[1], center[2]}));
    const camera::Viewport viewport{200, 160, 1.0};
    SceneCache cache;
    CpuBackend backend(4);

    const snapshot::Point near{1000.0, -1000.0, 0.0};
    Image before;
    backend.draw(cache.draw({.bodies = {torus}, .origin = near}, makeFrame(camera, viewport, near, kCanvas), kShaded),
                 2, before);
    const snapshot::Point moved{11000.0, 0.0, 0.0};
    Image after;
    backend.draw(
        cache.draw({.bodies = {torus, far}, .origin = moved}, makeFrame(camera, viewport, moved, kCanvas), kShaded), 2,
        after);

    const std::uint32_t background = backgroundOf(backend, makeFrame(camera, viewport, near, kCanvas));
    EXPECT_GT(before.pixels.size() - countOf(before, background), 4000U) << "the torus fills part of the view";
    std::size_t changed = 0;
    for (std::size_t i = 0; i < before.pixels.size(); ++i) {
        changed += before.pixels[i] != after.pixels[i] ? 1 : 0;
    }
    EXPECT_LE(changed, before.pixels.size() / 200) << "at most half a percent of the pixels may round differently";
}

TEST(CpuBackend, ReportsTheItemKindsItSkips)
{
    const snapshot::Point center{0.0, 0.0, 0.0};
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(test::torus(center)));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}};
    const Frame frame = makeFrame(lookingDownAt(center, {0.0, 1.0, 0.0}, 130.0), {64, 64, 1.0}, {}, kCanvas);
    SceneCache cache;
    DrawList list = cache.draw(scene, frame, kShaded);
    ASSERT_EQ(list.batches.size(), 1U);
    Batch edges = list.batches[0];
    edges.kind = ItemKind::Edges;
    Batch points = list.batches[0];
    points.kind = ItemKind::Points;
    list.batches.insert(list.batches.end(), {edges, points, edges});
    CpuBackend backend(2);
    Image image;
    const Report report = backend.draw(list, 1, image);
    EXPECT_EQ(report.skipped, (std::vector{ItemKind::Edges, ItemKind::Points}));
    EXPECT_LT(countOf(image, backgroundOf(backend, frame)), image.pixels.size()) << "faces still draw";
    EXPECT_TRUE(backend.draw(cache.draw(scene, frame, kShaded), 1, image).skipped.empty());
}

TEST(CpuBackend, ShadesWithTheLookFormula)
{
    const snapshot::Point center{0.0, 0.0, 0.0};
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(test::torus(center, 192, 96)));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}};
    const camera::Camera camera = lookingDownAt(center, {0.0, 1.0, 0.0}, 130.0);
    const camera::Viewport viewport{260, 260, 1.0};
    const Frame frame = makeFrame(camera, viewport, {}, kCanvas);
    SceneCache cache;
    CpuBackend backend(4);
    Image image;
    backend.draw(cache.draw(scene, frame, kShaded), 1, image);

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

} // namespace
} // namespace kalve137::render
