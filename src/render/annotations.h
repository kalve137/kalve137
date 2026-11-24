// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "render/positions.h"
#include "snapshot/color.h"
#include "snapshot/mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace kalve137::render {

using PixelOffset = std::array<float, 2>;
using AnnotationColor = std::array<std::uint8_t, 4>;

enum class BadgeIcon : std::uint8_t {
    Coincident,
    Equal,
    Horizontal,
    Lock,
    Midpoint,
    Parallel,
    Perpendicular,
    Symmetric,
    Tangent,
    Vertical
};
inline constexpr std::array kBadgeIcons{
    BadgeIcon::Coincident, BadgeIcon::Equal,         BadgeIcon::Horizontal, BadgeIcon::Lock,    BadgeIcon::Midpoint,
    BadgeIcon::Parallel,   BadgeIcon::Perpendicular, BadgeIcon::Symmetric,  BadgeIcon::Tangent, BadgeIcon::Vertical};

struct BadgeItem {
    camera::Vec3 anchor;
    PixelOffset offset{};
    BadgeIcon icon = BadgeIcon::Coincident;
    snapshot::Color iconColor;
    snapshot::Color fill;
    snapshot::Color border;

    bool operator==(const BadgeItem&) const = default;
};

struct LabelItem {
    camera::Vec3 anchor;
    PixelOffset offset{};
    std::u32string text;
    snapshot::Color color;
    snapshot::Color fill;
    snapshot::Color border;

    bool operator==(const LabelItem&) const = default;
};

struct Annotations {
    std::vector<BadgeItem> badges;
    std::vector<LabelItem> labels;

    bool operator==(const Annotations&) const = default;
};

struct Badge {
    Position position{};
    PixelOffset offset{};
    BadgeIcon icon = BadgeIcon::Coincident;
    AnnotationColor iconColor{};
    AnnotationColor fill{};
    AnnotationColor border{};

    bool operator==(const Badge&) const = default;
};

struct Label {
    Position position{};
    PixelOffset offset{};
    std::u32string glyphs;
    AnnotationColor color{};
    AnnotationColor fill{};
    AnnotationColor border{};

    bool operator==(const Label&) const = default;
};

struct PlacedAnnotations {
    std::vector<Badge> badges;
    std::vector<Label> labels;

    bool operator==(const PlacedAnnotations&) const = default;
};

PlacedAnnotations placeAnnotations(const Annotations& annotations, const snapshot::Point& origin);

struct AnnotationDraw {
    std::uint64_t revision = 0;
    std::span<const Badge> badges;
    std::span<const Label> labels;

    bool empty() const { return badges.empty() && labels.empty(); }
};

struct AtlasRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float left = 0.0F;
    float top = 0.0F;
    float advance = 0.0F;

    bool operator==(const AtlasRect&) const = default;
};

struct AtlasLayout {
    double ratio = 1.0;
    float ascent = 0.0F;
    float descent = 0.0F;
    std::array<AtlasRect, kBadgeIcons.size()> icons{};
    std::map<char32_t, AtlasRect> glyphs;
};

enum class QuadKind : std::uint8_t { Panel, Image };

struct AnnotationQuad {
    Position position{};
    PixelOffset corner{};
    std::array<float, 4> frame{};
    AnnotationColor color{};
    AnnotationColor border{};
    std::array<std::uint8_t, 4> style{};
    PixelOffset shape{};
};
static_assert(sizeof(AnnotationQuad) == 56);

std::size_t iconIndex(BadgeIcon icon);
int badgePixels(double ratio);
int badgeIconPixels(double ratio);
PixelOffset labelPixels(const Label& label, const AtlasLayout& atlas);
std::u32string missingGlyphs(std::span<const Label> labels, const AtlasLayout& atlas);
std::vector<AnnotationQuad> layoutAnnotations(const AnnotationDraw& draw, const AtlasLayout& atlas);

} // namespace kalve137::render
