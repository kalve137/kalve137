// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/handles.h"

#include "render/positions.h"
#include "render/style.h"

#include <algorithm>

namespace kalve137::render {

namespace {

constexpr float kByteMax = 255.0F;
constexpr float kPercent = 100.0F;

snapshot::Point pointOf(const camera::Vec3& point)
{
    return {point.x, point.y, point.z};
}

snapshot::Color withAlpha(snapshot::Color color, int alpha)
{
    color.alpha = static_cast<float>(alpha) / kByteMax;
    return color;
}

} // namespace

snapshot::Color lighter(const snapshot::Color& color, int percent)
{
    const float high = std::max({color.red, color.green, color.blue});
    const float low = std::min({color.red, color.green, color.blue});
    if (high <= 0.0F) {
        return color;
    }
    float value = high * static_cast<float>(percent) / kPercent;
    float saturation = (high - low) / high;
    if (value > 1.0F) {
        saturation = std::max(0.0F, saturation - (value - 1.0F));
        value = 1.0F;
    }
    const auto channel = [&](float c) {
        const float share = high > low ? (high - c) / (high - low) : 0.0F;
        return value * (1.0F - (saturation * share));
    };
    return {channel(color.red), channel(color.green), channel(color.blue), color.alpha};
}

Handle placeHandle(const HandleItem& item, const snapshot::Point& origin)
{
    const snapshot::Color fill = item.active ? item.colors.fill : item.colors.outline;
    const snapshot::Color outline = item.active ? item.colors.outline : item.colors.halo;
    const bool lit = item.hovered || item.dragged;
    return {.anchor = relativePosition(pointOf(item.anchor), origin),
            .toward = relativePosition(pointOf(item.toward), origin),
            .fill = vertexColor(lit ? lighter(fill, kHandleLitPercent) : fill),
            .outline = vertexColor(outline),
            .glow = vertexColor(withAlpha(fill, lit ? kHandleGlowAlpha : 0)),
            .arrow = {static_cast<float>(kHandleShaftPixels), static_cast<float>(kHandleShaftWidth),
                      static_cast<float>(kHandleHeadPixels), static_cast<float>(kHandleHeadWidth)},
            .stroke = {static_cast<float>(kHandleOutlineWidth), static_cast<float>(kHandleGlowWidth)}};
}

HandleGeometry placeHandles(std::span<const HandleItem> items, const snapshot::Point& origin)
{
    HandleGeometry geometry;
    for (const HandleItem& item : items) {
        geometry.arrows.push_back(placeHandle(item, origin));
        const VertexColor guide = vertexColor(withAlpha(item.colors.outline, kHandleGuideAlpha));
        for (std::size_t i = 1; i < item.guide.size(); ++i) {
            geometry.guides.push_back({.start = relativePosition(pointOf(item.guide[i - 1]), origin),
                                       .end = relativePosition(pointOf(item.guide[i]), origin),
                                       .color = guide,
                                       .width = static_cast<float>(kHandleGuideWidth),
                                       .pattern = static_cast<float>(kDotPattern)});
        }
    }
    return geometry;
}

} // namespace kalve137::render
