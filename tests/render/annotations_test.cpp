// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/annotations.h"
#include "render/style.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kalve137::render {
namespace {

constexpr AnnotationColor kInk{10, 20, 30, 255};
constexpr AnnotationColor kPaper{200, 200, 200, 235};
constexpr AnnotationColor kEdge{90, 90, 90, 255};

AtlasLayout atlasAt(double ratio)
{
    AtlasLayout atlas;
    atlas.ratio = ratio;
    atlas.ascent = 18.5F;
    atlas.descent = 4.9F;
    atlas.icons.at(iconIndex(BadgeIcon::Equal)) = {.x = 10, .y = 20, .width = 19, .height = 19};
    atlas.glyphs[U'1'] = {.x = 40, .y = 0, .width = 10, .height = 16, .left = 0.5F, .top = 14.0F, .advance = 11.2F};
    atlas.glyphs[U'2'] = {.x = 52, .y = 0, .width = 10, .height = 16, .left = 0.5F, .top = 14.0F, .advance = 11.2F};
    atlas.glyphs[U' '] = {.advance = 5.0F};
    return atlas;
}

AnnotationDraw drawOf(const std::vector<Badge>& badges, const std::vector<Label>& labels)
{
    return {.revision = 1, .badges = badges, .labels = labels};
}

TEST(Annotations, ABadgeIsARoundedPanelWithItsIconCentered)
{
    const std::vector<Badge> badges{{.icon = BadgeIcon::Equal, .iconColor = kInk, .fill = kPaper, .border = kEdge}};
    const std::vector<AnnotationQuad> quads = layoutAnnotations(drawOf(badges, {}), atlasAt(1.0));
    ASSERT_EQ(quads.size(), 2U);
    const AnnotationQuad& panel = quads[0];
    EXPECT_EQ(panel.style[0], static_cast<std::uint8_t>(QuadKind::Panel));
    EXPECT_EQ(panel.corner, (PixelOffset{-10.0F, -10.0F}));
    EXPECT_EQ(panel.frame, (std::array{20.0F, 20.0F, 0.0F, 0.0F}));
    EXPECT_FLOAT_EQ(panel.shape[0], 20.0F * static_cast<float>(kBadgeCornerShare));
    EXPECT_FLOAT_EQ(panel.shape[1], 1.0F);
    EXPECT_EQ(panel.color, kPaper);
    EXPECT_EQ(panel.border, kEdge);
    const AnnotationQuad& icon = quads[1];
    EXPECT_EQ(icon.style[0], static_cast<std::uint8_t>(QuadKind::Image));
    EXPECT_EQ(icon.corner, (PixelOffset{-10.0F, -10.0F}));
    EXPECT_EQ(icon.frame, (std::array{19.0F, 19.0F, 10.0F, 20.0F}));
    EXPECT_EQ(icon.color, kInk);
}

TEST(Annotations, BadgesScaleWithThePixelRatioAndSnapTheirOffset)
{
    EXPECT_EQ(badgePixels(1.0), kConstraintBadgePixels);
    EXPECT_EQ(badgePixels(1.5), 30);
    EXPECT_EQ(badgeIconPixels(1.0), 19);
    const std::vector<Badge> badges{{.offset = {3.3F, -2.0F}, .icon = BadgeIcon::Equal}};
    const std::vector<AnnotationQuad> quads = layoutAnnotations(drawOf(badges, {}), atlasAt(1.5));
    ASSERT_FALSE(quads.empty());
    EXPECT_EQ(quads[0].corner, (PixelOffset{-10.0F, -18.0F}));
    EXPECT_EQ(quads[0].frame[0], 30.0F);
    EXPECT_FLOAT_EQ(quads[0].shape[1], 1.5F);
}

TEST(Annotations, ALabelPadsItsTextToAnEvenSizeAndSetsGlyphsOnOneBaseline)
{
    const AtlasLayout atlas = atlasAt(1.0);
    const std::vector<Label> labels{{.glyphs = U"12", .color = kInk, .fill = kPaper, .border = kEdge}};
    EXPECT_EQ(labelPixels(labels[0], atlas), (PixelOffset{32.0F, 28.0F}));
    const std::vector<AnnotationQuad> quads = layoutAnnotations(drawOf({}, labels), atlas);
    ASSERT_EQ(quads.size(), 3U);
    EXPECT_EQ(quads[0].corner, (PixelOffset{-16.0F, -14.0F}));
    EXPECT_FLOAT_EQ(quads[0].shape[0], static_cast<float>(kLabelCorner));
    EXPECT_EQ(quads[1].corner, (PixelOffset{-11.0F, -7.0F}));
    EXPECT_EQ(quads[1].frame, (std::array{10.0F, 16.0F, 40.0F, 0.0F}));
    EXPECT_EQ(quads[2].corner[0], 1.0F);
    EXPECT_EQ(quads[2].corner[1], quads[1].corner[1]);
    EXPECT_EQ(quads[1].color, kInk);
}

TEST(Annotations, BlankGlyphsAdvanceWithoutAQuad)
{
    const std::vector<Label> labels{{.glyphs = U"1 2"}};
    EXPECT_EQ(layoutAnnotations(drawOf({}, labels), atlasAt(1.0)).size(), 3U);
}

TEST(Annotations, MissingGlyphsListEachUnknownCharacterOnce)
{
    const std::vector<Label> labels{{.glyphs = U"R12"}, {.glyphs = U"1\u00D8R"}};
    EXPECT_EQ(missingGlyphs(labels, atlasAt(1.0)), U"R\u00D8");
}

TEST(Annotations, PlacingMovesAnchorsNextToTheSceneOriginAndPacksColors)
{
    Annotations annotations;
    annotations.badges.push_back({.anchor = {101.0, 2.0, 3.0},
                                  .offset = {1.0F, 2.0F},
                                  .icon = BadgeIcon::Lock,
                                  .iconColor = {1.0F, 0.0F, 0.0F, 1.0F},
                                  .fill = {0.0F, 1.0F, 0.0F, 0.5F},
                                  .border = {0.0F, 0.0F, 1.0F, 1.0F}});
    annotations.labels.push_back({.anchor = {100.0, -1.0, 0.0}, .text = U"5", .color = {1.0F, 1.0F, 1.0F, 1.0F}});
    const PlacedAnnotations placed = placeAnnotations(annotations, {100.0, 0.0, 0.0});
    ASSERT_EQ(placed.badges.size(), 1U);
    EXPECT_EQ(placed.badges[0].position, (Position{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(placed.badges[0].offset, (PixelOffset{1.0F, 2.0F}));
    EXPECT_EQ(placed.badges[0].icon, BadgeIcon::Lock);
    EXPECT_EQ(placed.badges[0].iconColor, (AnnotationColor{255, 0, 0, 255}));
    EXPECT_EQ(placed.badges[0].fill[3], 128);
    ASSERT_EQ(placed.labels.size(), 1U);
    EXPECT_EQ(placed.labels[0].position, (Position{0.0F, -1.0F, 0.0F}));
    EXPECT_EQ(placed.labels[0].glyphs, U"5");
}

} // namespace
} // namespace kalve137::render
