// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/pick_id.h"
#include "render/style.h"
#include "snapshot/body.h"
#include "test_meshes.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kCanvas{0.1F, 0.2F, 0.3F};
constexpr ViewportStyle kShaded{.background = kCanvas,
                                .body = {0.6F, 0.65F, 0.7F},
                                .visualStyle = VisualStyle::Shaded,
                                .pixelRatio = 1.0,
                                .cullClosed = true};
constexpr camera::Viewport kViewport{240, 200, 1.0};
constexpr std::uint32_t kTopFace = 5;
constexpr int kFrontCorner = 5;
constexpr PickKinds kFacesAndEdges{.faces = true, .edges = true, .points = false};
constexpr PickKinds kEverything{.faces = true, .edges = true, .points = true};

using Pixel = std::array<double, 2>;

Pixel pixelOf(const Frame& frame, const snapshot::Point& point)
{
    const std::array<float, 16>& m = frame.viewProjection;
    std::array<double, 4> clip{};
    for (std::size_t r = 0; r < clip.size(); ++r) {
        clip[r] = (static_cast<double>(m[r]) * point[0]) + (static_cast<double>(m[r + 4]) * point[1]) +
                  (static_cast<double>(m[r + 8]) * point[2]) + static_cast<double>(m[r + 12]);
    }
    return {(clip[0] / clip[3] + 1.0) * 0.5 * frame.width, (1.0 - clip[1] / clip[3]) * 0.5 * frame.height};
}

Pixel toward(const Pixel& from, const Pixel& to, double distance)
{
    const double dx = to[0] - from[0];
    const double dy = to[1] - from[1];
    const double length = std::hypot(dx, dy);
    return {from[0] + (dx / length * distance), from[1] + (dy / length * distance)};
}

camera::Camera obliqueAt(const snapshot::Point& center, double scale)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center[0] + 300.0, center[1] - 200.0, center[2] + 250.0},
                                       {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(scale));
    return camera;
}

camera::Camera lookingDownAt(const snapshot::Point& center, double scale)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center[0], center[1], center[2] + 500.0}, {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
    EXPECT_TRUE(camera.setScale(scale));
    return camera;
}

class Picker {
public:
    Picker(const snapshot::Scene& scene, const Frame& frame, const IdStyle& style = {.points = true})
    {
        m_chunks.update(scene, kShaded);
        m_list = m_chunks.pickList(frame);
        CpuBackend backend(2);
        backend.drawIds(m_list, style, m_image);
    }

    std::optional<PickedItem> at(const Pixel& pixel, const PickKinds& kinds) const
    {
        const std::optional<IdHit> hit = resolvePick(m_image, static_cast<int>(std::floor(pixel[0])),
                                                     static_cast<int>(std::floor(pixel[1])), kPickTolerance, kinds);
        if (!hit) {
            return std::nullopt;
        }
        const PickId id = decodePickId(hit->id);
        if (id.chunk >= m_list.chunks.size()) {
            return std::nullopt;
        }
        return m_chunks.itemOf(m_list.chunks[id.chunk].id, id.kind, id.entry);
    }

    const IdImage& image() const { return m_image; }

private:
    ChunkSet m_chunks;
    DrawList m_list;
    IdImage m_image;
};

std::uint32_t edgeBetween(const snapshot::Mesh& mesh, const snapshot::Point& a, const snapshot::Point& b)
{
    for (const snapshot::Edge& edge : mesh.edges) {
        if ((edge.points.front() == a && edge.points.back() == b) ||
            (edge.points.front() == b && edge.points.back() == a)) {
            return edge.id;
        }
    }
    ADD_FAILURE() << "no edge between the corners";
    return 0;
}

PickedItem item(PickKind kind, std::uint32_t index, int feature = 1, int part = 0)
{
    return {.feature = feature, .part = part, .kind = kind, .index = index};
}

TEST(PickId, KeepsTheChunkTheKindAndTheEntry)
{
    const PickId id = decodePickId(encodePickId(kPickChunks - 1, PickKind::Point, kPaletteEntries - 1));
    EXPECT_EQ(id.chunk, kPickChunks - 1);
    EXPECT_EQ(id.kind, PickKind::Point);
    EXPECT_EQ(id.entry, kPaletteEntries - 1);
    EXPECT_NE(encodePickId(0, PickKind::Face, 0), 0U) << "zero stays empty space";
    EXPECT_EQ(decodePickId(0).kind, PickKind::None);
}

TEST(PickId, APointBeatsAnEdgeAndAnEdgeBeatsAFaceThenTheNearestWins)
{
    IdImage image{.width = 9, .height = 9, .ids = {}, .depths = {}};
    image.ids.assign(81, encodePickId(0, PickKind::Face, 1));
    image.depths.assign(81, 0.5F);
    const auto put = [&image](int x, int y, std::uint32_t id) { image.ids[image.indexOf(x, y)] = id; };
    const std::uint32_t far = encodePickId(0, PickKind::Edge, 2);
    const std::uint32_t near = encodePickId(0, PickKind::Edge, 3);
    const std::uint32_t point = encodePickId(1, PickKind::Point, 4);
    for (int y = 0; y < 9; ++y) {
        put(1, y, far);
        put(6, y, near);
    }
    put(8, 8, point);
    const auto pick = [&](const PickKinds& kinds) {
        const std::optional<IdHit> hit = resolvePick(image, 4, 4, kPickTolerance, kinds);
        return hit ? hit->id : 0U;
    };
    EXPECT_EQ(pick(kEverything), point) << "the point four pixels off each axis is in the square";
    EXPECT_EQ(pick(kFacesAndEdges), near) << "two pixels beat three";
    EXPECT_EQ(pick({.faces = true, .edges = false, .points = false}), encodePickId(0, PickKind::Face, 1));
    EXPECT_EQ(pick({.faces = false, .edges = false, .points = false}), 0U);
    const std::optional<IdHit> hit = resolvePick(image, 4, 4, kPickTolerance, kFacesAndEdges);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->x, 6);
    EXPECT_EQ(hit->y, 4) << "the nearest pixel of the edge, straight across";
}

TEST(CpuIds, PickAnEdgeFromThreePixelsAway)
{
    const auto mesh = test::edgedBox({-10.0, -10.0, -10.0}, {10.0, 10.0, 10.0});
    const snapshot::Scene scene{.bodies = {test::shared(test::bodyOf(mesh))}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(obliqueAt({}, 60.0), kViewport, {}, kCanvas);
    const Picker picker(scene, frame);
    const std::uint32_t frontTop = edgeBetween(*mesh, {10.0, -10.0, 10.0}, {10.0, 10.0, 10.0});
    const Pixel onEdge = pixelOf(frame, {10.0, 0.0, 10.0});
    const Pixel topCenter = pixelOf(frame, {0.0, 0.0, 10.0});
    EXPECT_EQ(picker.at(toward(onEdge, topCenter, 3.0), kFacesAndEdges), item(PickKind::Edge, frontTop));
    EXPECT_EQ(picker.at(toward(onEdge, topCenter, 8.0), kFacesAndEdges), item(PickKind::Face, kTopFace))
        << "past the tolerance the face wins";
}

TEST(CpuIds, APointBeatsAnEdgeAndAnEdgeBeatsAFace)
{
    const auto mesh = test::edgedBox({-10.0, -10.0, -10.0}, {10.0, 10.0, 10.0});
    const snapshot::Scene scene{.bodies = {test::shared(test::bodyOf(mesh))}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(obliqueAt({}, 60.0), kViewport, {}, kCanvas);
    const Picker picker(scene, frame);
    const Pixel corner = pixelOf(frame, {10.0, -10.0, 10.0});
    const Pixel nearCorner = toward(corner, pixelOf(frame, {0.0, 0.0, 10.0}), 2.0);
    EXPECT_EQ(picker.at(nearCorner, kEverything), item(PickKind::Point, kFrontCorner));
    const std::optional<PickedItem> edge = picker.at(nearCorner, kFacesAndEdges);
    ASSERT_TRUE(edge);
    EXPECT_EQ(edge->kind, PickKind::Edge);
    EXPECT_EQ(picker.at(nearCorner, {.faces = true, .edges = false, .points = false}), item(PickKind::Face, kTopFace));

    const Picker withoutPoints(scene, frame, {.points = false});
    EXPECT_EQ(withoutPoints.at(nearCorner, kEverything)->kind, PickKind::Edge) << "points draw only when asked";
}

TEST(CpuIds, TheNearestEdgeWins)
{
    const auto mesh = test::edgedBox({-10.0, -1.0, -10.0}, {10.0, 1.0, 10.0});
    const auto body = test::shared(test::bodyOf(mesh));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(lookingDownAt({}, 90.0), kViewport, {}, kCanvas);
    const Picker picker(scene, frame);
    const Pixel upper = pixelOf(frame, {0.0, 1.0, 10.0});
    const Pixel lower = pixelOf(frame, {0.0, -1.0, 10.0});
    const double gap = std::hypot(upper[0] - lower[0], upper[1] - lower[1]);
    ASSERT_GT(gap, 4.0);
    ASSERT_LT(gap, kPickTolerance + 1.0) << "from a pixel inside either edge, both edges sit in one pick square";
    const std::uint32_t upperEdge = edgeBetween(*mesh, {-10.0, 1.0, 10.0}, {10.0, 1.0, 10.0});
    const std::uint32_t lowerEdge = edgeBetween(*mesh, {-10.0, -1.0, 10.0}, {10.0, -1.0, 10.0});
    EXPECT_EQ(picker.at(toward(upper, lower, 1.0), kFacesAndEdges), item(PickKind::Edge, upperEdge));
    EXPECT_EQ(picker.at(toward(lower, upper, 1.0), kFacesAndEdges), item(PickKind::Edge, lowerEdge));
}

TEST(CpuIds, HiddenBodiesAndEmptySpacePickNothing)
{
    const auto shown = test::shared(test::bodyOf(test::edgedBox({-10.0, -10.0, -10.0}, {10.0, 10.0, 10.0})));
    const auto hidden = test::shared(test::bodyOf(test::edgedBox({30.0, -10.0, -10.0}, {50.0, 10.0, 10.0}), {}, 1));
    const snapshot::Scene scene{.bodies = {shown}, .origin = {}, .hidden = {hidden}};
    const Frame frame = makeFrame(lookingDownAt({20.0, 0.0, 0.0}, 90.0), kViewport, {}, kCanvas);
    const Picker picker(scene, frame);
    EXPECT_EQ(picker.at(pixelOf(frame, {0.0, 0.0, 10.0}), kEverything), item(PickKind::Face, kTopFace));
    EXPECT_FALSE(picker.at(pixelOf(frame, {40.0, 0.0, 10.0}), kEverything)) << "the hidden box";
    EXPECT_FALSE(picker.at({2.0, 2.0}, kEverything)) << "the corner of the view";
}

TEST(CpuIds, MapBackToTheBodyInASharedChunk)
{
    const auto first = test::shared(test::bodyOf(test::edgedBox({-30.0, -10.0, -10.0}, {-10.0, 10.0, 10.0})));
    snapshot::Body second = test::bodyOf(test::edgedBox({10.0, -10.0, -10.0}, {30.0, 10.0, 10.0}), {}, 3);
    second.feature = 7;
    const auto shared = test::shared(second);
    const snapshot::Scene scene{.bodies = {first, shared}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(lookingDownAt({}, 90.0), kViewport, {}, kCanvas);
    ChunkSet chunks;
    chunks.update(scene, kShaded);
    ASSERT_EQ(chunks.chunkCount(), 1U) << "both boxes share a chunk";
    const Picker picker(scene, frame);
    EXPECT_EQ(picker.at(pixelOf(frame, {-20.0, 0.0, 10.0}), kEverything), item(PickKind::Face, kTopFace, 1, 0));
    EXPECT_EQ(picker.at(pixelOf(frame, {20.0, 0.0, 10.0}), kEverything), item(PickKind::Face, kTopFace, 7, 3));
    EXPECT_EQ(picker.at(pixelOf(frame, {30.0, 10.0, 10.0}), kEverything), item(PickKind::Point, 7, 7, 3));
}

TEST(CpuIds, DepthsRunFromNearToFar)
{
    const auto body = test::shared(test::bodyOf(test::edgedBox({-10.0, -10.0, -10.0}, {10.0, 10.0, 10.0})));
    const snapshot::Scene scene{.bodies = {body}, .origin = {}, .hidden = {}};
    const Frame frame = makeFrame(obliqueAt({}, 60.0), kViewport, {}, kCanvas);
    const Picker picker(scene, frame);
    const Pixel front = pixelOf(frame, {10.0, -10.0, 10.0});
    const Pixel back = pixelOf(frame, {-10.0, 10.0, 10.0});
    const IdImage& image = picker.image();
    const Pixel inFront = toward(front, back, 4.0);
    const Pixel inBack = toward(back, front, 4.0);
    const float near = image.depthAt(static_cast<int>(inFront[0]), static_cast<int>(inFront[1]));
    const float far = image.depthAt(static_cast<int>(inBack[0]), static_cast<int>(inBack[1]));
    EXPECT_GT(near, 0.0F);
    EXPECT_LT(near, far);
    EXPECT_LT(far, 1.0F);
    EXPECT_EQ(image.depthAt(1, 1), 1.0F) << "empty space is at the far plane";
}

} // namespace
} // namespace kalve137::render
