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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
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

} // namespace
} // namespace kalve137::render
