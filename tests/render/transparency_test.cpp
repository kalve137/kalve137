// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/draw_list.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr snapshot::Color kBody{0.6F, 0.65F, 0.7F};
constexpr float kGhost = 0.45F;

ViewportStyle shaded()
{
    return {.background = kCanvas,
            .body = kBody,
            .visualStyle = VisualStyle::Shaded,
            .pixelRatio = 1.0,
            .cullClosed = true};
}

std::shared_ptr<const snapshot::Body> ghostBox(int part, double low, float opacity = kGhost)
{
    snapshot::Body body = test::bodyOf(test::box({low, 0.0, 0.0}, {low + 2.0, 2.0, 2.0}), {}, part);
    body.opacity = opacity;
    return test::shared(std::move(body));
}

Frame frameFrom(const camera::Vec3& eye, camera::Projection projection)
{
    camera::Camera camera;
    camera.setProjectionType(projection);
    EXPECT_TRUE(camera.setEyeAndCenter(eye, {2.0, 1.0, 1.0}));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    return makeFrame(camera, {640, 480, 1.0}, {}, kCanvas);
}

std::vector<float> drawnCentersX(const DrawList& list)
{
    std::vector<float> centers;
    centers.reserve(list.transparent.size());
    for (const TransparentDraw& draw : list.transparent) {
        centers.push_back(list.chunks.at(draw.chunk).ranges[draw.range].center[0]);
    }
    return centers;
}

TEST(Transparency, SortsOverlappingBodiesFarToNearFromEitherEye)
{
    const auto middle = ghostBox(0, 1.0);
    const auto left = ghostBox(1, 0.0);
    const auto right = ghostBox(2, 2.0);
    ChunkSet chunks;
    chunks.update({.bodies = {middle, left, right}, .origin = {}, .hidden = {}}, shaded());
    for (const camera::Projection projection : {camera::Projection::Perspective, camera::Projection::Orthographic}) {
        const DrawList fromRight = chunks.draw(frameFrom({60.0, 20.0, 15.0}, projection));
        ASSERT_EQ(fromRight.transparent.size(), 3U);
        EXPECT_EQ(drawnCentersX(fromRight), (std::vector<float>{1.0F, 2.0F, 3.0F}));

        const DrawList fromLeft = chunks.draw(frameFrom({-60.0, -20.0, 15.0}, projection));
        EXPECT_EQ(drawnCentersX(fromLeft), (std::vector<float>{3.0F, 2.0F, 1.0F}));
    }
}

TEST(Transparency, AnOpacityChangeKeepsTheMeshAndMovesTheBodyToItsOwnChunk)
{
    const auto solid = ghostBox(0, 0.0, 1.0F);
    const auto other = ghostBox(1, 4.0, 1.0F);
    ChunkSet chunks;
    chunks.update({.bodies = {solid, other}, .origin = {}, .hidden = {}}, shaded());
    const Frame frame = frameFrom({60.0, 20.0, 15.0}, camera::Projection::Perspective);
    const DrawList opaque = chunks.draw(frame);
    ASSERT_EQ(opaque.chunks.size(), 1U);
    EXPECT_TRUE(opaque.transparent.empty());

    snapshot::Body faded = *solid;
    faded.opacity = kGhost;
    const auto ghost = test::shared(std::move(faded));
    EXPECT_EQ(ghost->mesh, solid->mesh);
    chunks.update({.bodies = {ghost, other}, .origin = {}, .hidden = {}}, shaded());
    const DrawList mixed = chunks.draw(frame);
    ASSERT_EQ(mixed.chunks.size(), 2U);
    ASSERT_EQ(mixed.transparent.size(), 1U);
    const Chunk& seeThrough = mixed.chunks.at(mixed.transparent[0].chunk);
    EXPECT_TRUE(seeThrough.transparent());
    EXPECT_FLOAT_EQ(seeThrough.opacity, kGhost);
    EXPECT_EQ(seeThrough.ranges.size(), 1U);
    EXPECT_EQ(seeThrough.ranges[0].indexCount, seeThrough.indices.size());

    const DrawList picks = chunks.pickList(frame);
    EXPECT_EQ(picks.chunks.size(), 2U) << "half transparent model body stays pickable";
    EXPECT_TRUE(picks.transparent.empty());
}

TEST(Transparency, HiddenBodiesDrawNoTransparentPass)
{
    const auto shown = ghostBox(0, 0.0);
    const auto hidden = ghostBox(1, 4.0);
    ChunkSet chunks;
    chunks.update({.bodies = {shown}, .origin = {}, .hidden = {hidden}}, shaded());
    const DrawList list = chunks.draw(frameFrom({60.0, 20.0, 15.0}, camera::Projection::Perspective));
    EXPECT_EQ(list.transparent.size(), 1U);
}

TEST(Transparency, OpacityClampsToTheUnitRange)
{
    snapshot::Body body;
    body.opacity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(opacityOf(body), 1.0F);
    body.opacity = -0.5F;
    EXPECT_FLOAT_EQ(opacityOf(body), 0.0F);
    body.opacity = 3.0F;
    EXPECT_FLOAT_EQ(opacityOf(body), 1.0F);
    body.opacity = kGhost;
    EXPECT_FLOAT_EQ(opacityOf(body), kGhost);
}

} // namespace
} // namespace kalve137::render
