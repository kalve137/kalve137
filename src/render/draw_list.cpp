// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "render/draw_list.h"

#include <algorithm>
#include <cmath>

namespace kalve137::render {

namespace {

constexpr double kLightFromUp = 0.6;
constexpr double kLightFromRight = 0.35;
constexpr float kByte = 255.0F;
constexpr float kLinearLimit = 0.04045F;
constexpr float kEncodedLimit = 0.0031308F;
constexpr float kLinearSlope = 12.92F;
constexpr float kGamma = 2.4F;
constexpr float kOffset = 0.055F;

Normal toNormal(const camera::Vec3& vector)
{
    return {static_cast<float>(vector.x), static_cast<float>(vector.y), static_cast<float>(vector.z)};
}

float dot(const Normal& a, const Normal& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Normal normalized(const Normal& vector)
{
    const float length = std::sqrt(dot(vector, vector));
    if (length <= 0.0F) {
        return vector;
    }
    return {vector[0] / length, vector[1] / length, vector[2] / length};
}

std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByte));
}

} // namespace

Frame makeFrame(const camera::Camera& camera, const camera::Viewport& viewport, const snapshot::Point& origin,
                const snapshot::Color& background, const Look& look)
{
    camera::Camera shown = camera;
    if (viewport.isValid()) {
        shown.setAspect(viewport.aspect());
    }
    const camera::Vec3 eye = -shown.direction();
    const camera::Vec3 up = shown.orthogonalizedUp();
    const camera::Vec3 right = camera::cross(shown.direction(), up);
    const camera::Vec3 eyePosition = shown.eye() - camera::Vec3{origin[0], origin[1], origin[2]};
    Frame frame;
    frame.viewProjection = (shown.projection() * shown.viewRelativeTo({origin[0], origin[1], origin[2]})).toFloat();
    frame.eye = toNormal(eye);
    frame.eyePosition = toNormal(eyePosition);
    frame.perspective = !shown.isOrthographic();
    frame.light = toNormal(camera::normalized(eye + up * kLightFromUp + right * kLightFromRight));
    frame.look = look;
    frame.background = background;
    frame.width = viewport.width;
    frame.height = viewport.height;
    frame.pixelRatio = viewport.pixelRatio;
    return frame;
}

Normal viewAt(const Position& position, const Frame& frame)
{
    if (!frame.perspective) {
        return frame.eye;
    }
    return normalized(
        {frame.eyePosition[0] - position[0], frame.eyePosition[1] - position[1], frame.eyePosition[2] - position[2]});
}

Linear lit(const Normal& normal, const Normal& view, const Linear& color, const Frame& frame)
{
    Normal side = normal;
    if (dot(side, view) < 0.0F) {
        side = {-side[0], -side[1], -side[2]};
    }
    const Look& look = frame.look;
    const float diffuse = std::max(0.0F, dot(side, frame.light));
    float specular = 0.0F;
    if (diffuse > 0.0F) {
        const Normal half = normalized({frame.light[0] + view[0], frame.light[1] + view[1], frame.light[2] + view[2]});
        specular = std::pow(std::max(0.0F, dot(side, half)), look.shininess);
    }
    const float level = look.ambient + (look.diffuse * diffuse);
    return {(color[0] * level) + (look.specular[0] * specular), (color[1] * level) + (look.specular[1] * specular),
            (color[2] * level) + (look.specular[2] * specular)};
}

float toLinear(float encoded)
{
    return encoded <= kLinearLimit ? encoded / kLinearSlope : std::pow((encoded + kOffset) / (1.0F + kOffset), kGamma);
}

float toSrgb(float linear)
{
    const float value = std::clamp(linear, 0.0F, 1.0F);
    return value <= kEncodedLimit ? value * kLinearSlope
                                  : ((1.0F + kOffset) * std::pow(value, 1.0F / kGamma)) - kOffset;
}

Linear linearOf(const VertexColor& color)
{
    const auto channel = [](std::uint8_t value) { return toLinear(static_cast<float>(value) / kByte); };
    return {channel(color[0]), channel(color[1]), channel(color[2])};
}

VertexColor vertexColor(const snapshot::Color& color)
{
    return {toByte(color.red), toByte(color.green), toByte(color.blue), toByte(color.alpha)};
}

std::array<std::uint8_t, 2> encodeNormal(const Normal& normal)
{
    const float sum = std::abs(normal[0]) + std::abs(normal[1]) + std::abs(normal[2]);
    if (sum <= 0.0F) {
        return {toByte(0.5F), toByte(0.5F)};
    }
    float x = normal[0] / sum;
    float y = normal[1] / sum;
    if (normal[2] < 0.0F) {
        const float foldedX = (1.0F - std::abs(y)) * (x >= 0.0F ? 1.0F : -1.0F);
        const float foldedY = (1.0F - std::abs(x)) * (y >= 0.0F ? 1.0F : -1.0F);
        x = foldedX;
        y = foldedY;
    }
    return {toByte((x * 0.5F) + 0.5F), toByte((y * 0.5F) + 0.5F)};
}

Normal decodeNormal(std::uint8_t x, std::uint8_t y)
{
    Normal normal{(static_cast<float>(x) / kByte * 2.0F) - 1.0F, (static_cast<float>(y) / kByte * 2.0F) - 1.0F, 0.0F};
    normal[2] = 1.0F - std::abs(normal[0]) - std::abs(normal[1]);
    const float fold = std::max(-normal[2], 0.0F);
    normal[0] += normal[0] >= 0.0F ? -fold : fold;
    normal[1] += normal[1] >= 0.0F ? -fold : fold;
    return normalized(normal);
}

Vertex packVertex(const Position& position, const Normal& normal, std::uint32_t entry)
{
    const std::array<std::uint8_t, 2> encoded = encodeNormal(normal);
    return {.position = position,
            .packed = {encoded[0], encoded[1], static_cast<std::uint8_t>(entry & 0xFFU),
                       static_cast<std::uint8_t>((entry >> 8) & 0xFFU)}};
}

std::uint32_t entryOf(const Vertex& vertex)
{
    return static_cast<std::uint32_t>(vertex.packed[2]) | (static_cast<std::uint32_t>(vertex.packed[3]) << 8);
}

Segment packSegment(const Position& start, const Position& end, std::uint32_t entry)
{
    return {
        .start = start,
        .end = end,
        .packed = {static_cast<std::uint8_t>(entry & 0xFFU), static_cast<std::uint8_t>((entry >> 8) & 0xFFU), 0, 0}};
}

std::uint32_t entryOf(const Segment& segment)
{
    return static_cast<std::uint32_t>(segment.packed[0]) | (static_cast<std::uint32_t>(segment.packed[1]) << 8);
}

} // namespace kalve137::render
