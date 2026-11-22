// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/draw_list.h"
#include "render/scene_cache.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr snapshot::Color kBody{0.6F, 0.65F, 0.7F};
constexpr float kTolerance = 1e-5F;

ViewportStyle styleOf(VisualStyle visual)
{
    return {.background = kCanvas, .body = kBody, .visualStyle = visual, .pixelRatio = 1.0};
}

Frame frameAt(const snapshot::Point& origin)
{
    return makeFrame(camera::Camera(), {640, 480, 1.0}, origin, kCanvas);
}

std::set<VertexColor> colorsOf(const Batch& batch)
{
    return {batch.colors.data.begin(), batch.colors.data.end()};
}

TEST(DrawList, ShadedPaintsTheThemeColorThenTheBodyColorThenFaceColors)
{
    const snapshot::Color red{1.0F, 0.0F, 0.0F};
    const snapshot::Color green{0.0F, 1.0F, 0.0F};
    const auto mesh = test::box({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0});
    snapshot::Appearance faces{.color = red, .faces = {}};
    faces.faces.resize(6);
    faces.faces[3] = green;
    const auto themed = std::make_shared<const snapshot::Body>(test::bodyOf(mesh));
    const auto colored = std::make_shared<const snapshot::Body>(test::bodyOf(mesh, {.color = red, .faces = {}}));
    const auto painted = std::make_shared<const snapshot::Body>(test::bodyOf(mesh, faces));
    const snapshot::Scene scene{.bodies = {themed, colored, painted}, .origin = {}};
    SceneCache cache;
    for (const VisualStyle visual : {VisualStyle::Shaded, VisualStyle::ShadedWithEdges}) {
        const DrawList list = cache.draw(scene, frameAt({}), styleOf(visual));
        ASSERT_EQ(list.batches.size(), 3U);
        for (const Batch& batch : list.batches) {
            EXPECT_EQ(batch.kind, ItemKind::Faces);
            EXPECT_TRUE(batch.lit);
            EXPECT_EQ(batch.positions.data.size(), mesh->vertices.size());
            EXPECT_EQ(batch.triangles.data.size(), mesh->triangles.size());
        }
        EXPECT_EQ(colorsOf(list.batches[0]), std::set{vertexColor(kBody)});
        EXPECT_EQ(colorsOf(list.batches[1]), std::set{vertexColor({1.0F, 0.0F, 0.0F})});
        const Batch& paintedBatch = list.batches[2];
        for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
            const snapshot::Color expected = mesh->faces[i] == 3 ? green : red;
            for (const std::uint32_t index : mesh->triangles[i]) {
                EXPECT_EQ(paintedBatch.colors.data[index], vertexColor(expected));
            }
        }
    }
}

TEST(DrawList, VisibleEdgesFillsBodiesWithTheCanvasUnlit)
{
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(
        test::box({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}), {.color = snapshot::Color{1.0F, 0.0F, 0.0F}, .faces = {}}));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}};
    SceneCache cache;
    const DrawList list = cache.draw(scene, frameAt({}), styleOf(VisualStyle::VisibleEdges));
    ASSERT_EQ(list.batches.size(), 1U);
    EXPECT_FALSE(list.batches[0].lit);
    EXPECT_EQ(colorsOf(list.batches[0]), std::set{vertexColor(kCanvas)});
}

TEST(DrawList, WireframeDrawsNoFaces)
{
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(test::box({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}};
    SceneCache cache;
    EXPECT_TRUE(cache.draw(scene, frameAt({}), styleOf(VisualStyle::Wireframe)).batches.empty());
}

TEST(DrawList, PositionsAreRelativeToTheSceneOrigin)
{
    const snapshot::Point origin{1000000.0, -2000000.0, 3000000.0};
    const auto mesh = test::box({1000001.0, -1999998.0, 3000003.0}, {1000002.0, -1999997.0, 3000004.0});
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(mesh));
    SceneCache cache;
    const DrawList list =
        cache.draw({.bodies = {body}, .origin = origin}, frameAt(origin), styleOf(VisualStyle::Shaded));
    ASSERT_EQ(list.batches.size(), 1U);
    for (std::size_t i = 0; i < mesh->vertices.size(); ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            EXPECT_EQ(list.batches[0].positions.data[i][axis],
                      static_cast<float>(mesh->vertices[i][axis] - origin[axis]));
        }
    }
}

TEST(SceneCache, AColorEditKeepsThePositionsAndTheMesh)
{
    const auto mesh = test::box({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(mesh));
    SceneCache cache;
    const ViewportStyle style = styleOf(VisualStyle::Shaded);
    const Batch first = cache.draw({.bodies = {body}, .origin = {}}, frameAt({}), style).batches.at(0);
    const auto recolored = std::make_shared<const snapshot::Body>(
        test::bodyOf(mesh, {.color = snapshot::Color{1.0F, 0.0F, 0.0F}, .faces = {}}));
    const Batch second = cache.draw({.bodies = {recolored}, .origin = {}}, frameAt({}), style).batches.at(0);
    EXPECT_EQ(second.positions.id, first.positions.id);
    EXPECT_EQ(second.positions.data.data(), first.positions.data.data());
    EXPECT_EQ(second.triangles.id, first.triangles.id);
    EXPECT_NE(second.colors.id, first.colors.id);
    const Batch again = cache.draw({.bodies = {recolored}, .origin = {}}, frameAt({}), style).batches.at(0);
    EXPECT_EQ(again.colors.id, second.colors.id);

    ViewportStyle light = style;
    light.body = {0.9F, 0.9F, 0.9F};
    const Batch themed = cache.draw({.bodies = {body}, .origin = {}}, frameAt({}), light).batches.at(0);
    EXPECT_EQ(themed.positions.id, first.positions.id);
    EXPECT_EQ(colorsOf(themed), std::set{vertexColor(light.body)});
}

TEST(SceneCache, AnOriginMoveRebuildsOnlyThePositions)
{
    const auto body = std::make_shared<const snapshot::Body>(test::bodyOf(test::box({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0})));
    SceneCache cache;
    const ViewportStyle style = styleOf(VisualStyle::Shaded);
    const Batch first = cache.draw({.bodies = {body}, .origin = {}}, frameAt({}), style).batches.at(0);
    const snapshot::Point moved{1000.0, 0.0, 0.0};
    const Batch second = cache.draw({.bodies = {body}, .origin = moved}, frameAt(moved), style).batches.at(0);
    EXPECT_NE(second.positions.id, first.positions.id);
    EXPECT_EQ(second.positions.data[0][0], -1000.0F);
    EXPECT_EQ(second.normals.id, first.normals.id);
    EXPECT_EQ(second.colors.id, first.colors.id);
}

TEST(Light, FollowsTheLookFormula)
{
    camera::Camera camera;
    ASSERT_TRUE(camera.setEyeAndCenter({0.0, 0.0, 100.0}, {0.0, 0.0, 0.0}));
    ASSERT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
    const Frame frame = makeFrame(camera, {640, 480, 1.0}, {}, kCanvas);
    const double length = std::sqrt((1.0 * 1.0) + (0.6 * 0.6) + (0.35 * 0.35));
    EXPECT_NEAR(frame.light[0], 0.35 / length, kTolerance) << "the light comes from the viewer's right";
    EXPECT_NEAR(frame.light[1], 0.6 / length, kTolerance);
    EXPECT_NEAR(frame.light[2], 1.0 / length, kTolerance);
    EXPECT_FALSE(frame.perspective);

    const Linear gray{0.5F, 0.25F, 0.125F};
    const Normal toEye{0.0F, 0.0F, 1.0F};
    const double half =
        (1.0 / length + 1.0) / std::sqrt((0.35 * 0.35 / (length * length)) + (0.36 / (length * length)) +
                                         ((1.0 / length + 1.0) * (1.0 / length + 1.0)));
    const double shine = std::pow(half, 83.2);
    const Linear facing = lit(toEye, frame.eye, gray, frame);
    for (std::size_t i = 0; i < 3; ++i) {
        const double specular = std::array{0.992, 0.941, 0.808}[i];
        EXPECT_NEAR(facing[i], (gray[i] * (0.0875 + (1.0 / length))) + (specular * shine), kTolerance);
    }
    EXPECT_EQ(lit({0.0F, 0.0F, -1.0F}, frame.eye, gray, frame), facing) << "back faces shade like front faces";
    const Linear away = lit({-1.0F, 0.0F, 0.0F}, frame.eye, gray, frame);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(away[i], gray[i] * 0.0875F, kTolerance) << "a side turned from the light gets ambient";
    }
}

TEST(Light, EncodesLikeSrgb)
{
    EXPECT_NEAR(toSrgb(0.5F), 0.735357F, kTolerance);
    EXPECT_NEAR(toSrgb(0.002F), 0.02584F, kTolerance);
    EXPECT_NEAR(toSrgb(2.0F), 1.0F, kTolerance) << "values past white clamp";
    for (const float value : {0.0F, 0.01F, 0.2F, 0.5F, 0.9F, 1.0F}) {
        EXPECT_NEAR(toSrgb(toLinear(value)), value, kTolerance);
    }
    EXPECT_NEAR(linearOf({0x8A, 0x90, 0x99, 0xFF})[0], 0.254152F, kTolerance);
}

TEST(Light, PerspectiveViewsFromTheEye)
{
    camera::Camera camera;
    ASSERT_TRUE(camera.setEyeAndCenter({10.0, 0.0, 100.0}, {10.0, 0.0, 0.0}));
    camera.setProjectionType(camera::Projection::Perspective);
    const Frame frame = makeFrame(camera, {640, 480, 1.0}, {10.0, 0.0, 0.0}, kCanvas);
    EXPECT_TRUE(frame.perspective);
    const Normal view = viewAt({0.0F, 0.0F, 0.0F}, frame);
    EXPECT_NEAR(view[2], 1.0F, kTolerance) << "the eye sits straight above the origin";
    const Normal slanted = viewAt({100.0F, 0.0F, 0.0F}, frame);
    EXPECT_NEAR(slanted[0], -std::sqrt(0.5F), kTolerance);
}

} // namespace
} // namespace kalve137::render
