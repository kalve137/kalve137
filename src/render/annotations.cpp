// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/annotations.h"

#include "render/draw_list.h"
#include "render/style.h"

#include <cmath>

namespace kalve137::render {

namespace {

PixelOffset scaled(const PixelOffset& offset, double ratio)
{
    return {static_cast<float>(std::lround(offset[0] * ratio)), static_cast<float>(std::lround(offset[1] * ratio))};
}

AnnotationQuad panel(const Position& position, PixelOffset corner, PixelOffset size, const AnnotationColor& fill,
                     const AnnotationColor& border, double radius, double ratio)
{
    return {.position = position,
            .corner = corner,
            .frame = {size[0], size[1], 0.0F, 0.0F},
            .color = fill,
            .border = border,
            .style = {static_cast<std::uint8_t>(QuadKind::Panel), 0, 0, 0},
            .shape = {static_cast<float>(radius), static_cast<float>(kAnnotationStroke * ratio)}};
}

AnnotationQuad image(const Position& position, PixelOffset corner, const AtlasRect& rect, const AnnotationColor& color)
{
    return {.position = position,
            .corner = corner,
            .frame = {static_cast<float>(rect.width), static_cast<float>(rect.height), static_cast<float>(rect.x),
                      static_cast<float>(rect.y)},
            .color = color,
            .border = {},
            .style = {static_cast<std::uint8_t>(QuadKind::Image), 0, 0, 0},
            .shape = {}};
}

float advanceOf(const std::u32string& glyphs, const AtlasLayout& atlas)
{
    float advance = 0.0F;
    for (const char32_t glyph : glyphs) {
        if (const auto found = atlas.glyphs.find(glyph); found != atlas.glyphs.end()) {
            advance += found->second.advance;
        }
    }
    return advance;
}

int evenCeil(double value)
{
    const int whole = static_cast<int>(std::ceil(value));
    return whole + (whole % 2);
}

void addBadge(const Badge& badge, const AtlasLayout& atlas, std::vector<AnnotationQuad>& quads)
{
    const int side = badgePixels(atlas.ratio);
    const PixelOffset center = scaled(badge.offset, atlas.ratio);
    const int half = side / 2;
    const PixelOffset corner{center[0] - static_cast<float>(half), center[1] - static_cast<float>(half)};
    quads.push_back(panel(badge.position, corner, {static_cast<float>(side), static_cast<float>(side)}, badge.fill,
                          badge.border, side * kBadgeCornerShare, atlas.ratio));
    const AtlasRect& icon = atlas.icons.at(iconIndex(badge.icon));
    if (icon.width <= 0 || icon.height <= 0) {
        return;
    }
    const int inset = (side - badgeIconPixels(atlas.ratio)) / 2;
    quads.push_back(image(badge.position,
                          {corner[0] + static_cast<float>(inset), corner[1] + static_cast<float>(inset)}, icon,
                          badge.iconColor));
}

void addLabel(const Label& label, const AtlasLayout& atlas, std::vector<AnnotationQuad>& quads)
{
    const PixelOffset size = labelPixels(label, atlas);
    const PixelOffset center = scaled(label.offset, atlas.ratio);
    const PixelOffset corner{center[0] - (size[0] / 2.0F), center[1] - (size[1] / 2.0F)};
    quads.push_back(
        panel(label.position, corner, size, label.fill, label.border, kLabelCorner * atlas.ratio, atlas.ratio));
    const float line = atlas.ascent + atlas.descent;
    const auto baseline = static_cast<float>(std::lround(((size[1] - line) / 2.0F) + atlas.ascent));
    float pen = (size[0] - advanceOf(label.glyphs, atlas)) / 2.0F;
    for (const char32_t glyph : label.glyphs) {
        const auto found = atlas.glyphs.find(glyph);
        if (found == atlas.glyphs.end()) {
            continue;
        }
        const AtlasRect& rect = found->second;
        if (rect.width > 0 && rect.height > 0) {
            const PixelOffset at{corner[0] + static_cast<float>(std::lround(pen + rect.left)),
                                 corner[1] + baseline - static_cast<float>(std::lround(rect.top))};
            quads.push_back(image(label.position, at, rect, label.color));
        }
        pen += rect.advance;
    }
}

} // namespace

PlacedAnnotations placeAnnotations(const Annotations& annotations, const snapshot::Point& origin)
{
    PlacedAnnotations placed;
    placed.badges.reserve(annotations.badges.size());
    for (const BadgeItem& item : annotations.badges) {
        placed.badges.push_back({.position = relativePosition({item.anchor.x, item.anchor.y, item.anchor.z}, origin),
                                 .offset = item.offset,
                                 .icon = item.icon,
                                 .iconColor = vertexColor(item.iconColor),
                                 .fill = vertexColor(item.fill),
                                 .border = vertexColor(item.border)});
    }
    placed.labels.reserve(annotations.labels.size());
    for (const LabelItem& item : annotations.labels) {
        placed.labels.push_back({.position = relativePosition({item.anchor.x, item.anchor.y, item.anchor.z}, origin),
                                 .offset = item.offset,
                                 .glyphs = item.text,
                                 .color = vertexColor(item.color),
                                 .fill = vertexColor(item.fill),
                                 .border = vertexColor(item.border)});
    }
    return placed;
}

std::size_t iconIndex(BadgeIcon icon)
{
    return static_cast<std::size_t>(icon);
}

int badgePixels(double ratio)
{
    return static_cast<int>(std::lround(kConstraintBadgePixels * ratio));
}

int badgeIconPixels(double ratio)
{
    return static_cast<int>(std::lround(badgePixels(ratio) * kBadgeIconShare));
}

PixelOffset labelPixels(const Label& label, const AtlasLayout& atlas)
{
    const double padX = kLabelPadX * atlas.ratio;
    const double padY = kLabelPadY * atlas.ratio;
    return {static_cast<float>(evenCeil(advanceOf(label.glyphs, atlas) + (2.0 * padX))),
            static_cast<float>(evenCeil(atlas.ascent + atlas.descent + (2.0 * padY)))};
}

std::u32string missingGlyphs(std::span<const Label> labels, const AtlasLayout& atlas)
{
    std::u32string missing;
    for (const Label& label : labels) {
        for (const char32_t glyph : label.glyphs) {
            if (!atlas.glyphs.contains(glyph) && missing.find(glyph) == std::u32string::npos) {
                missing.push_back(glyph);
            }
        }
    }
    return missing;
}

std::vector<AnnotationQuad> layoutAnnotations(const AnnotationDraw& draw, const AtlasLayout& atlas)
{
    std::vector<AnnotationQuad> quads;
    quads.reserve((2 * draw.badges.size()) + draw.labels.size());
    for (const Badge& badge : draw.badges) {
        addBadge(badge, atlas, quads);
    }
    for (const Label& label : draw.labels) {
        addLabel(label, atlas, quads);
    }
    return quads;
}

} // namespace kalve137::render
