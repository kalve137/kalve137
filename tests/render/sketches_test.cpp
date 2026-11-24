// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/datums.h"
#include "render/pick_id.h"
#include "render/sketches.h"
#include "render/style.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace kalve137::render {
namespace {

constexpr snapshot::Color kFree{0.9F, 0.9F, 0.9F};
constexpr snapshot::Color kConstruction{0.9F, 0.6F, 0.2F};
constexpr snapshot::Color kProfile{0.4F, 0.6F, 0.9F, 0.25F};
constexpr snapshot::Color kHighlight{0.2F, 0.5F, 1.0F, 0.6F};
constexpr snapshot::Color kHover{0.5F, 0.8F, 1.0F, 0.6F};

SketchLook look(bool lines, bool points, bool profiles)
{
    return {.free = kFree,
            .construction = kConstruction,
            .profile = kProfile,
            .highlight = kHighlight,
            .hover = kHover,
            .editing = 0,
            .lines = lines,
            .points = points,
            .profiles = profiles,
            .highlighted = {}};
}

snapshot::Sketch sketch(int feature)
{
    snapshot::Sketch sketch;
    sketch.feature = feature;
    sketch.curves = {
        {.id = 1, .kind = snapshot::SketchCurveKind::Line, .construction = false, .points = {{0, 0, 0}, {10, 0, 0}}},
        {.id = 2, .kind = snapshot::SketchCurveKind::Line, .construction = true, .points = {{0, 0, 0}, {0, 10, 0}}},
        {.id = 3,
         .kind = snapshot::SketchCurveKind::Circle,
         .construction = false,
         .points = {{5, 0, 0}, {0, 5, 0}, {-5, 0, 0}}},
    };
    sketch.points = {{.id = 7, .point = {10, 0, 0}, .flags = 0}};
    sketch.profiles = {.vertices = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, 0}},
                       .triangles = {{0, 1, 2}, {1, 3, 2}},
                       .profiles = {0, 1},
                       .count = 2};
    return sketch;
}

std::uint32_t entryOf(const SketchGeometry& geometry, const SketchKey& key)
{
    for (std::size_t i = 0; i < geometry.keys.size(); ++i) {
        if (geometry.keys[i] == key) {
            return static_cast<std::uint32_t>(i);
        }
    }
    ADD_FAILURE() << "no entry for the key";
    return 0;
}

TEST(Sketches, CurvesTakeTheirWidthAndDashAndOnlyLinesPickInLineModes)
{
    const std::vector sketches{sketch(4)};
    const SketchGeometry idle = buildSketches(sketches, look(false, false, false), {}, {});
    ASSERT_EQ(idle.curves.size(), 4U);
    EXPECT_FLOAT_EQ(idle.curves[0].width, static_cast<float>(kSketchCurveWidth));
    EXPECT_FLOAT_EQ(idle.curves[0].pattern, static_cast<float>(kSolidPattern));
    EXPECT_EQ(idle.curves[0].color, vertexColor(kFree));
    EXPECT_FLOAT_EQ(idle.curves[1].width, static_cast<float>(kSketchConstructionWidth));
    EXPECT_FLOAT_EQ(idle.curves[1].pattern, static_cast<float>(kDashPattern));
    EXPECT_EQ(idle.curves[1].color, vertexColor(kConstruction));
    EXPECT_TRUE(idle.idCurves.empty());
    EXPECT_TRUE(idle.keys.empty());
    EXPECT_TRUE(idle.profiles.empty());
    EXPECT_TRUE(idle.markers.empty());

    const SketchGeometry lines = buildSketches(sketches, look(true, false, false), {}, {});
    ASSERT_EQ(lines.keys.size(), 2U) << "the circle does not pick as a line";
    EXPECT_EQ(lines.keys[0], (SketchKey{.item = SketchItem::Line, .feature = 4, .index = 1}));
    EXPECT_EQ(lines.keys[1], (SketchKey{.item = SketchItem::Line, .feature = 4, .index = 2}));
    ASSERT_EQ(lines.idCurves.size(), 2U);
    EXPECT_GE(lines.idCurves[1].width, static_cast<float>(kPickEdgeWidth));
    EXPECT_FLOAT_EQ(lines.idCurves[1].pattern, static_cast<float>(kSolidPattern))
        << "a dashed line picks along its gaps";
    EXPECT_EQ(lines.idCurves[1].color[0], 1U);
    EXPECT_EQ(lines.idCurves[1].color[2], static_cast<std::uint8_t>(PickKind::Edge));
    EXPECT_EQ(sketchAt(lines, 1), lines.keys[1]);
    EXPECT_FALSE(sketchAt(lines, 2).has_value());
}

TEST(Sketches, CurvesSitAtTheSceneOrigin)
{
    const std::vector sketches{sketch(4)};
    const SketchGeometry moved = buildSketches(sketches, look(false, false, false), {}, {10.0, 0.0, 0.0});
    EXPECT_EQ(moved.curves[0].start, (Position{-10.0F, 0.0F, 0.0F}));
    EXPECT_EQ(moved.curves[0].end, (Position{0.0F, 0.0F, 0.0F}));
}

TEST(Sketches, TheEditedSketchIsLeftToTheEditor)
{
    std::vector sketches{sketch(4), sketch(6)};
    SketchLook editing = look(true, true, true);
    editing.editing = 6;
    const SketchGeometry geometry = buildSketches(sketches, editing, {}, {});
    EXPECT_EQ(geometry.curves.size(), 4U);
    for (const SketchKey& key : geometry.keys) {
        EXPECT_EQ(key.feature, 4);
    }
}

TEST(Sketches, PointsAreSmallTopmostRingsThatPickInTheSketchChunk)
{
    const std::vector sketches{sketch(4)};
    EXPECT_TRUE(buildSketches(sketches, look(true, false, true), {}, {}).markers.empty());
    const SketchGeometry geometry = buildSketches(sketches, look(false, true, false), {}, {});
    ASSERT_EQ(geometry.markers.size(), 1U);
    const MarkerItem& marker = geometry.markers[0];
    EXPECT_EQ(marker.shape, MarkerShape::Ring);
    EXPECT_EQ(marker.scale, MarkerScale::Small);
    EXPECT_EQ(marker.color, kFree);
    EXPECT_EQ(marker.flags, kMarkerTopmost | kMarkerPickable);
    const PickId id = decodePickId(marker.pick);
    EXPECT_EQ(id.chunk, kSketchChunk);
    EXPECT_EQ(id.kind, PickKind::Point);
    const std::optional<SketchKey> key = sketchAt(geometry, id.entry);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, (SketchKey{.item = SketchItem::Point, .feature = 4, .index = 7}));
    const std::optional<snapshot::Point> point = sketchPointOf(sketches, *key);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(*point, (snapshot::Point{10, 0, 0}));
    EXPECT_FALSE(sketchPointOf(sketches, {.item = SketchItem::Line, .feature = 4, .index = 1}).has_value());

    const SketchGeometry hovered =
        buildSketches(sketches, look(false, true, false), {.hovered = *key, .selected = {}}, {});
    EXPECT_NE(hovered.markers[0].color, kFree) << "a hovered point lights up";
}

TEST(Sketches, ProfilesFillInThePlaneColorAndHighlightedOnesInTheAccent)
{
    const std::vector sketches{sketch(4)};
    SketchLook profiles = look(false, false, true);
    profiles.highlighted = {{4, 1}};
    const SketchGeometry geometry = buildSketches(sketches, profiles, {}, {});
    ASSERT_EQ(geometry.profiles.size(), 6U);
    EXPECT_EQ(geometry.profiles[0].color, vertexColor(kProfile));
    EXPECT_EQ(geometry.profiles[3].color, vertexColor(kHighlight));
    ASSERT_EQ(geometry.idProfiles.size(), 6U);
    const std::uint32_t second = entryOf(geometry, {.item = SketchItem::Profile, .feature = 4, .index = 1});
    EXPECT_EQ(geometry.idProfiles[3].color[0], static_cast<std::uint8_t>(second));
    EXPECT_EQ(geometry.idProfiles[3].color[2], static_cast<std::uint8_t>(PickKind::Face));

    const SketchKey first{.item = SketchItem::Profile, .feature = 4, .index = 0};
    const SketchGeometry hovered = buildSketches(sketches, profiles, {.hovered = first, .selected = {}}, {});
    EXPECT_EQ(hovered.profiles[0].color, vertexColor(kHover));
    const SketchGeometry selected =
        buildSketches(sketches, look(false, false, true), {.hovered = {}, .selected = {first}}, {});
    EXPECT_EQ(selected.profiles[0].color, vertexColor(kHighlight));
    EXPECT_EQ(selected.profiles[3].color, vertexColor(kProfile));
    EXPECT_FALSE(selected.drawsLike(geometry));
    EXPECT_TRUE(geometry.drawsLike(buildSketches(sketches, profiles, {}, {})));
}

} // namespace
} // namespace kalve137::render
