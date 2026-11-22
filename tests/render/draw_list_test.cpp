// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/chunk_set.h"
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
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr snapshot::Color kBody{0.6F, 0.65F, 0.7F};
constexpr snapshot::Color kRed{1.0F, 0.0F, 0.0F};
constexpr snapshot::Color kGreen{0.0F, 1.0F, 0.0F};
constexpr float kTolerance = 1e-5F;
constexpr std::size_t kBoxVertices = 24;
constexpr std::size_t kBoxTriangles = 12;

ViewportStyle styleOf(VisualStyle visual)
{
    return {.background = kCanvas, .body = kBody, .visualStyle = visual, .pixelRatio = 1.0, .cullClosed = true};
}

Frame frameAt(const snapshot::Point& origin)
{
    return makeFrame(camera::Camera(), {640, 480, 1.0}, origin, kCanvas);
}

const Entry& entryAt(const Chunk& chunk, std::size_t index)
{
    return chunk.palette[entryOf(chunk.vertices[chunk.indices[index]])];
}

VertexColor colorAt(const Chunk& chunk, std::size_t index)
{
    const Entry& entry = entryAt(chunk, index);
    return {entry[0], entry[1], entry[2], 255};
}

std::set<VertexColor> colorsOf(const Chunk& chunk)
{
    std::set<VertexColor> colors;
    for (std::size_t i = 0; i < chunk.indices.size(); ++i) {
        colors.insert(colorAt(chunk, i));
    }
    return colors;
}

std::shared_ptr<const snapshot::Body> boxBody(int part, const snapshot::Point& low = {0.0, 0.0, 0.0},
                                              snapshot::Appearance appearance = {})
{
    return test::shared(
        test::bodyOf(test::box(low, {low[0] + 1.0, low[1] + 1.0, low[2] + 1.0}), std::move(appearance), part));
}

DrawList drawn(ChunkSet& chunks, const snapshot::Scene& scene, const ViewportStyle& style)
{
    chunks.update(scene, style);
    return chunks.draw(frameAt(scene.origin));
}

TEST(DrawList, PacksNormalsAndEntriesIntoSixteenBytes)
{
    double worst = 1.0;
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j <= 32; ++j) {
            const double a = 2.0 * std::numbers::pi * i / 64;
            const double b = (std::numbers::pi * j / 32) - (std::numbers::pi / 2);
            const Normal normal{static_cast<float>(std::cos(b) * std::cos(a)),
                                static_cast<float>(std::cos(b) * std::sin(a)), static_cast<float>(std::sin(b))};
            const Vertex vertex = packVertex({1.0F, 2.0F, 3.0F}, normal, 0xBEEFU);
            const Normal back = decodeNormal(vertex.packed[0], vertex.packed[1]);
            worst = std::min(
                worst, static_cast<double>((normal[0] * back[0]) + (normal[1] * back[1]) + (normal[2] * back[2])));
            EXPECT_EQ(entryOf(vertex), 0xBEEFU);
        }
    }
    EXPECT_GT(worst, std::cos(1.5 * std::numbers::pi / 180.0)) << "a packed normal turns by less than 1.5 degrees";
}

TEST(ChunkSet, ShadedPaintsTheThemeColorThenTheBodyColorThenFaceColors)
{
    snapshot::Appearance faces{.color = kRed, .faces = {}};
    faces.faces.resize(6);
    faces.faces[3] = kGreen;
    const auto themed = boxBody(0);
    const auto colored = boxBody(1, {}, {.color = kRed, .faces = {}});
    const auto painted = boxBody(2, {}, faces);
    const snapshot::Scene scene{.bodies = {themed, colored, painted}, .origin = {}, .hidden = {}};
    const auto mesh = scene.bodies[0]->mesh;
    ChunkSet chunks;
    for (const VisualStyle visual : {VisualStyle::Shaded, VisualStyle::ShadedWithEdges}) {
        const DrawList list = drawn(chunks, scene, styleOf(visual));
        EXPECT_TRUE(list.lit);
        ASSERT_EQ(list.chunks.size(), 1U) << "three small bodies share one chunk";
        const Chunk& chunk = list.chunks[0];
        EXPECT_TRUE(chunk.closed);
        EXPECT_EQ(chunk.vertices.size(), std::size_t{3} * kBoxVertices);
        ASSERT_EQ(chunk.indices.size(), std::size_t{9} * kBoxTriangles);
        for (std::size_t i = 0; i < kBoxTriangles; ++i) {
            EXPECT_EQ(colorAt(chunk, 3 * i), vertexColor(kBody));
            EXPECT_EQ(colorAt(chunk, 3 * (kBoxTriangles + i)), vertexColor(kRed));
            const snapshot::Color expected = mesh->faces[i] == 3 ? kGreen : kRed;
            EXPECT_EQ(colorAt(chunk, 3 * ((2 * kBoxTriangles) + i)), vertexColor(expected));
        }
    }
}

TEST(ChunkSet, VisibleEdgesFillsBodiesWithTheCanvasUnlit)
{
    ChunkSet chunks;
    const auto body = boxBody(0, {}, {.color = kRed, .faces = {}});
    const DrawList list = drawn(chunks, {.bodies = {body}, .origin = {}}, styleOf(VisualStyle::VisibleEdges));
    ASSERT_EQ(list.chunks.size(), 1U);
    EXPECT_FALSE(list.lit);
    EXPECT_EQ(colorsOf(list.chunks[0]), std::set{vertexColor(kCanvas)});
}

TEST(ChunkSet, WireframeDrawsNoFaces)
{
    ChunkSet chunks;
    const auto body = boxBody(0);
    EXPECT_TRUE(drawn(chunks, {.bodies = {body}, .origin = {}}, styleOf(VisualStyle::Wireframe)).chunks.empty());
}

TEST(ChunkSet, PositionsAreRelativeToTheSceneOrigin)
{
    const snapshot::Point origin{1000000.0, -2000000.0, 3000000.0};
    const auto body = boxBody(0, {1000001.0, -1999998.0, 3000003.0});
    ChunkSet chunks;
    const DrawList list = drawn(chunks, {.bodies = {body}, .origin = origin}, styleOf(VisualStyle::Shaded));
    ASSERT_EQ(list.chunks.size(), 1U);
    std::set<Position> expected;
    for (const snapshot::Point& vertex : body->mesh->vertices) {
        expected.insert(relativePosition(vertex, origin));
    }
    std::set<Position> seen;
    for (const Vertex& vertex : list.chunks[0].vertices) {
        seen.insert(vertex.position);
    }
    EXPECT_EQ(seen, expected);
}

TEST(ChunkSet, AColorEditOrAThemeChangeRewritesOnlyThePalette)
{
    const auto body = boxBody(0);
    ChunkSet chunks;
    const ViewportStyle style = styleOf(VisualStyle::Shaded);
    const Chunk first = drawn(chunks, {.bodies = {body}, .origin = {}}, style).chunks.at(0);
    const auto recolored = test::shared(test::bodyOf(body->mesh, {.color = kRed, .faces = {}}));
    const Chunk second = drawn(chunks, {.bodies = {recolored}, .origin = {}}, style).chunks.at(0);
    EXPECT_EQ(second.meshRevision, first.meshRevision);
    EXPECT_EQ(second.vertices.data(), first.vertices.data());
    EXPECT_NE(second.paletteRevision, first.paletteRevision);
    EXPECT_EQ(colorsOf(second), std::set{vertexColor(kRed)});
    const Chunk again = drawn(chunks, {.bodies = {recolored}, .origin = {}}, style).chunks.at(0);
    EXPECT_EQ(again.paletteRevision, second.paletteRevision) << "the same scene changes nothing";

    ViewportStyle light = style;
    light.body = {0.9F, 0.9F, 0.9F};
    const Chunk themed = drawn(chunks, {.bodies = {body}, .origin = {}}, light).chunks.at(0);
    EXPECT_EQ(themed.meshRevision, first.meshRevision);
    EXPECT_EQ(colorsOf(themed), std::set{vertexColor(light.body)});
    EXPECT_EQ(chunks.meshBuilds(), 1U);
}

TEST(ChunkSet, HidingABodyClearsItsEntriesAndKeepsTheMesh)
{
    const auto shown = boxBody(0);
    const auto other = boxBody(1, {5.0, 0.0, 0.0});
    ChunkSet chunks;
    const ViewportStyle style = styleOf(VisualStyle::Shaded);
    const Chunk first = drawn(chunks, {.bodies = {shown, other}, .origin = {}}, style).chunks.at(0);
    const DrawList hidden = drawn(chunks, {.bodies = {other}, .origin = {}, .hidden = {shown}}, style);
    ASSERT_EQ(hidden.chunks.size(), 1U);
    const Chunk& chunk = hidden.chunks[0];
    EXPECT_EQ(chunk.meshRevision, first.meshRevision);
    EXPECT_NE(chunk.paletteRevision, first.paletteRevision);
    for (std::size_t i = 0; i < chunk.indices.size(); ++i) {
        const bool visible = (entryAt(chunk, i)[3] & kEntryVisible) != 0;
        EXPECT_EQ(visible, i >= 3 * kBoxTriangles) << "index " << i;
    }
    EXPECT_TRUE(drawn(chunks, {.bodies = {}, .origin = {}, .hidden = {shown, other}}, style).chunks.empty())
        << "a chunk with nothing visible is not drawn";
    EXPECT_EQ(chunks.chunkCount(), 1U);
    EXPECT_EQ(chunks.meshBuilds(), 1U);
}

TEST(ChunkSet, MovingOneBodyRebuildsOnlyItsChunk)
{
    ChunkSet chunks(2 * kBoxVertices);
    const ViewportStyle style = styleOf(VisualStyle::Shaded);
    std::vector<std::shared_ptr<const snapshot::Body>> bodies;
    bodies.reserve(6);
    for (int part = 0; part < 6; ++part) {
        bodies.push_back(boxBody(part, {2.0 * part, 0.0, 0.0}));
    }
    const DrawList before = drawn(chunks, {.bodies = bodies, .origin = {}}, style);
    ASSERT_EQ(before.chunks.size(), 3U) << "two boxes fit the budget of each chunk";
    bodies[3] = boxBody(3, {6.0, 5.0, 0.0});
    const DrawList after = drawn(chunks, {.bodies = bodies, .origin = {}}, style);
    ASSERT_EQ(after.chunks.size(), 3U);
    int rebuilt = 0;
    for (std::size_t i = 0; i < after.chunks.size(); ++i) {
        EXPECT_EQ(after.chunks[i].id, before.chunks[i].id);
        rebuilt += after.chunks[i].meshRevision != before.chunks[i].meshRevision ? 1 : 0;
    }
    EXPECT_EQ(rebuilt, 1);

    bodies.erase(bodies.begin() + 2, bodies.begin() + 4);
    const DrawList removed = drawn(chunks, {.bodies = bodies, .origin = {}}, style);
    EXPECT_EQ(removed.chunks.size(), 2U) << "an empty chunk goes away";
    EXPECT_EQ(chunks.triangleCount(), 4 * kBoxTriangles);
}

TEST(ChunkSet, AnOriginMoveRebuildsEveryMesh)
{
    const auto body = boxBody(0);
    ChunkSet chunks;
    const ViewportStyle style = styleOf(VisualStyle::Shaded);
    const Chunk first = drawn(chunks, {.bodies = {body}, .origin = {}}, style).chunks.at(0);
    const snapshot::Point moved{1000.0, 0.0, 0.0};
    const Chunk second = drawn(chunks, {.bodies = {body}, .origin = moved}, style).chunks.at(0);
    EXPECT_NE(second.meshRevision, first.meshRevision);
    for (const Vertex& vertex : second.vertices) {
        EXPECT_LE(vertex.position[0], -999.0F);
    }
}

TEST(ChunkSet, OpenAndClosedBodiesKeepSeparateChunks)
{
    const auto closed = boxBody(0);
    const auto open = test::shared(test::bodyOf(test::box({3.0, 0.0, 0.0}, {4.0, 1.0, 1.0}, false), {}, 1));
    ChunkSet chunks;
    const DrawList list = drawn(chunks, {.bodies = {closed, open}, .origin = {}}, styleOf(VisualStyle::Shaded));
    ASSERT_EQ(list.chunks.size(), 2U);
    EXPECT_NE(list.chunks[0].closed, list.chunks[1].closed);
    EXPECT_TRUE(list.cullClosed);
    ViewportStyle section = styleOf(VisualStyle::Shaded);
    section.cullClosed = false;
    EXPECT_FALSE(drawn(chunks, {.bodies = {closed, open}, .origin = {}}, section).cullClosed);
}

TEST(ChunkSet, AVertexSharedByTwoFacesIsSplit)
{
    snapshot::Mesh mesh;
    mesh.vertices = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
    mesh.faces = {0, 1};
    mesh.faceCount = 2;
    snapshot::Appearance appearance{.color = kRed, .faces = {std::nullopt, kGreen}};
    const auto body =
        test::shared(test::bodyOf(std::make_shared<const snapshot::Mesh>(std::move(mesh)), std::move(appearance)));
    ChunkSet chunks;
    const Chunk chunk = drawn(chunks, {.bodies = {body}, .origin = {}}, styleOf(VisualStyle::Shaded)).chunks.at(0);
    EXPECT_EQ(chunk.vertices.size(), 6U) << "the diagonal's two vertices belong to both faces";
    EXPECT_EQ(colorAt(chunk, 0), vertexColor(kRed));
    EXPECT_EQ(colorAt(chunk, 3), vertexColor(kGreen));
}

snapshot::Color colorOfFace(std::uint32_t face)
{
    return {static_cast<float>(face & 0xFFU) / 255.0F, static_cast<float>((face >> 8) & 0xFFU) / 255.0F,
            static_cast<float>((face >> 16) & 0xFFU) / 255.0F};
}

TEST(ChunkSet, ABodyAboveThePaletteLimitKeepsEveryFaceColor)
{
    const auto mesh = test::grid({0.0, 0.0, 0.0}, 100.0, 257);
    ASSERT_GT(mesh->faceCount, kPaletteEntries);
    snapshot::Appearance appearance;
    for (std::uint32_t face = 0; face < mesh->faceCount; ++face) {
        appearance.faces.emplace_back(colorOfFace(face));
    }
    const auto body = test::shared(test::bodyOf(mesh, std::move(appearance)));
    ChunkSet chunks;
    const DrawList list = drawn(chunks, {.bodies = {body}, .origin = {}}, styleOf(VisualStyle::Shaded));
    ASSERT_EQ(list.chunks.size(), 2U) << "the body splits into two pieces";
    std::size_t triangle = 0;
    for (const Chunk& chunk : list.chunks) {
        EXPECT_LE(chunk.palette.size(), kPaletteEntries);
        for (std::size_t i = 0; i < chunk.indices.size(); i += 3, ++triangle) {
            const std::uint32_t face = mesh->faces[triangle];
            ASSERT_EQ(colorAt(chunk, i), vertexColor(colorOfFace(face))) << "face " << face;
        }
    }
    EXPECT_EQ(triangle, mesh->triangles.size());

    for (const std::uint32_t face : {0U, kPaletteEntries - 1, kPaletteEntries, mesh->faceCount - 1}) {
        const std::optional<FaceEntry> found = chunks.entryOf(1, 0, face);
        ASSERT_TRUE(found) << face;
        const auto chunk = std::ranges::find(list.chunks, found->chunk, &Chunk::id);
        ASSERT_NE(chunk, list.chunks.end());
        const Entry& entry = chunk->palette[found->entry];
        EXPECT_EQ((VertexColor{entry[0], entry[1], entry[2], 255}), vertexColor(colorOfFace(face))) << face;
    }
    EXPECT_FALSE(chunks.entryOf(1, 0, mesh->faceCount));
    EXPECT_FALSE(chunks.entryOf(2, 0, 0));
}

TEST(ChunkSet, APixelRatioChangeShowsInTheNextDrawAndRebuildsNothing)
{
    ChunkSet chunks;
    ViewportStyle style = styleOf(VisualStyle::Shaded);
    const auto body = boxBody(0);
    const snapshot::Scene scene{.bodies = {body}, .origin = {}};
    EXPECT_EQ(drawn(chunks, scene, style).pixelRatio, 1.0);
    const std::uint64_t meshes = chunks.meshBuilds();
    const std::uint64_t palettes = chunks.paletteBuilds();
    style.pixelRatio = 2.0;
    EXPECT_EQ(drawn(chunks, scene, style).pixelRatio, 2.0);
    EXPECT_EQ(chunks.meshBuilds(), meshes);
    EXPECT_EQ(chunks.paletteBuilds(), palettes);
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
